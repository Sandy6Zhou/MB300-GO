#include "my_common.h"

#define Y_SOH       0x01
#define Y_STX       0x02
#define Y_EOT       0x04
#define Y_ACK       0x06
#define Y_NAK       0x15
#define Y_CAN       0x18
#define Y_CRC_REQ   0x43
#define Y_PAD       0x1A

#define Y_TIMEOUT_MS 1000
#define Y_MAX_RETRY  10

enum {
    Y_STATE_WAIT_C = 0,
    Y_STATE_WAIT_HEADER_ACK,
    Y_STATE_WAIT_DATA_C,
    Y_STATE_WAIT_DATA_ACK,
    Y_STATE_WAIT_EOT_NAK,
    Y_STATE_WAIT_EOT_ACK,
    Y_STATE_WAIT_FINAL_C,
    Y_STATE_WAIT_FINAL_ACK,
};

static uint16 y_crc16(const uint8 *data, uint32 len)
{
    uint16 crc = 0;
    uint8 bit;

    while (len--) {
        crc ^= (uint16)(*data++) << 8;
        for (bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) ? (uint16)((crc << 1) ^ 0x1021) : (uint16)(crc << 1);
        }
    }
    return crc;
}

static void y_fail(ymodem_sender_t *ctx, const char *error)
{
    ctx->result = YMODEM_RESULT_ERROR;
    ctx->error = error;
}

static int y_send_frame(ymodem_sender_t *ctx)
{
    if (ctx->send == NULL || ctx->frame_len == 0 ||
        ctx->send(ctx->priv, ctx->frame, ctx->frame_len) != ctx->frame_len) {
        y_fail(ctx, "uart send failed");
        return -1;
    }
    ctx->wait_ms = 0;
    return 0;
}

static int y_make_packet(ymodem_sender_t *ctx, uint8 block_no, uint16 payload_len)
{
    uint16 crc;

    ctx->frame[0] = (payload_len == 128) ? Y_SOH : Y_STX;
    ctx->frame[1] = block_no;
    ctx->frame[2] = (uint8)(0xFF - block_no);
    crc = y_crc16(&ctx->frame[3], payload_len);
    ctx->frame[3 + payload_len] = (uint8)(crc >> 8);
    ctx->frame[4 + payload_len] = (uint8)crc;
    ctx->frame_len = payload_len + 5;
    return y_send_frame(ctx);
}

static int y_send_header(ymodem_sender_t *ctx, uint8 empty)
{
    uint8 *payload = &ctx->frame[3];
    int n;
    uint32 name_len;

    memset(payload, 0, 128);
    if (!empty) {
        name_len = strlen(ctx->file_name);
        if (name_len > 100) {
            name_len = 100;
        }
        memcpy(payload, ctx->file_name, name_len);
        n = snprintf((char *)&payload[name_len + 1], 128 - name_len - 1,
                     "%u", (unsigned)ctx->file_size);
        if (n <= 0) {
            y_fail(ctx, "invalid file size");
            return -1;
        }
    }
    return y_make_packet(ctx, 0, 128);
}

static int y_send_data(ymodem_sender_t *ctx)
{
    uint32 remain = ctx->file_size - ctx->offset;
    uint32 want = remain > YMODEM_PACKET_1K_SIZE ? YMODEM_PACKET_1K_SIZE : remain;
    int got;

    memset(&ctx->frame[3], Y_PAD, YMODEM_PACKET_1K_SIZE);
    got = ctx->read(ctx->priv, ctx->offset, &ctx->frame[3], want);
    if (got != (int)want) {
        y_fail(ctx, "firmware read failed");
        return -1;
    }
    ctx->data_len = (uint16)want;
    return y_make_packet(ctx, ctx->block_no, YMODEM_PACKET_1K_SIZE);
}

static int y_send_eot(ymodem_sender_t *ctx)
{
    ctx->frame[0] = Y_EOT;
    ctx->frame_len = 1;
    return y_send_frame(ctx);
}

static void y_set_state(ymodem_sender_t *ctx, uint8 state)
{
    ctx->state = state;
    ctx->retries = 0;
    ctx->wait_ms = 0;
}

void ymodem_sender_init(ymodem_sender_t *ctx,
                        const char *file_name,
                        uint32 file_size,
                        ymodem_send_cb_t send_cb,
                        ymodem_read_cb_t read_cb,
                        ymodem_progress_cb_t progress_cb,
                        void *priv)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->send = send_cb;
    ctx->read = read_cb;
    ctx->progress = progress_cb;
    ctx->priv = priv;
    ctx->file_name = file_name;
    ctx->file_size = file_size;
    ctx->block_no = 1;
    ctx->state = Y_STATE_WAIT_C;
    ctx->result = YMODEM_RESULT_RUNNING;
}

void ymodem_sender_input(ymodem_sender_t *ctx, const uint8 *data, uint32 len)
{
    uint8 ch;

    if (ctx == NULL || data == NULL || ctx->result != YMODEM_RESULT_RUNNING) {
        return;
    }

    while (len-- && ctx->result == YMODEM_RESULT_RUNNING) {
        ch = *data++;
        if (ch == Y_CAN) {
            if (++ctx->can_count >= 2) {
                y_fail(ctx, "receiver cancelled");
            }
            continue;
        }
        ctx->can_count = 0;

        switch (ctx->state) {
        case Y_STATE_WAIT_C:
            if (ch == Y_CRC_REQ && y_send_header(ctx, 0) == 0) {
                y_set_state(ctx, Y_STATE_WAIT_HEADER_ACK);
            }
            break;

        case Y_STATE_WAIT_HEADER_ACK:
            if (ch == Y_ACK) {
                y_set_state(ctx, Y_STATE_WAIT_DATA_C);
            } else if (ch == Y_NAK) {
                y_send_frame(ctx);
            }
            break;

        case Y_STATE_WAIT_DATA_C:
            if (ch == Y_CRC_REQ) {
                if (ctx->file_size == 0) {
                    if (y_send_eot(ctx) == 0) y_set_state(ctx, Y_STATE_WAIT_EOT_NAK);
                } else if (y_send_data(ctx) == 0) {
                    y_set_state(ctx, Y_STATE_WAIT_DATA_ACK);
                }
            }
            break;

        case Y_STATE_WAIT_DATA_ACK:
            if (ch == Y_ACK) {
                ctx->offset += ctx->data_len;
                if (ctx->progress) ctx->progress(ctx->priv, ctx->offset, ctx->file_size);
                if (ctx->offset >= ctx->file_size) {
                    if (y_send_eot(ctx) == 0) y_set_state(ctx, Y_STATE_WAIT_EOT_NAK);
                } else {
                    ctx->block_no++;
                    if (y_send_data(ctx) == 0) y_set_state(ctx, Y_STATE_WAIT_DATA_ACK);
                }
            } else if (ch == Y_NAK) {
                y_send_frame(ctx);
            }
            break;

        case Y_STATE_WAIT_EOT_NAK:
            if (ch == Y_NAK) {
                if (y_send_eot(ctx) == 0) y_set_state(ctx, Y_STATE_WAIT_EOT_ACK);
            } else if (ch == Y_ACK) {
                y_set_state(ctx, Y_STATE_WAIT_FINAL_C);
            }
            break;

        case Y_STATE_WAIT_EOT_ACK:
            if (ch == Y_ACK) {
                y_set_state(ctx, Y_STATE_WAIT_FINAL_C);
            } else if (ch == Y_NAK) {
                y_send_frame(ctx);
            }
            break;

        case Y_STATE_WAIT_FINAL_C:
            if (ch == Y_CRC_REQ && y_send_header(ctx, 1) == 0) {
                y_set_state(ctx, Y_STATE_WAIT_FINAL_ACK);
            }
            break;

        case Y_STATE_WAIT_FINAL_ACK:
            if (ch == Y_ACK) {
                ctx->result = YMODEM_RESULT_DONE;
                ctx->wait_ms = 0;
            } else if (ch == Y_NAK) {
                y_send_frame(ctx);
            }
            break;
        }
    }
}

void ymodem_sender_tick(ymodem_sender_t *ctx, uint16 elapsed_ms)
{
    if (ctx == NULL || ctx->result != YMODEM_RESULT_RUNNING) {
        return;
    }
    ctx->wait_ms += elapsed_ms;
    if (ctx->wait_ms < Y_TIMEOUT_MS) {
        return;
    }
    ctx->wait_ms = 0;
    if (++ctx->retries > Y_MAX_RETRY) {
        y_fail(ctx, "receiver response timeout");
        return;
    }

    if (ctx->state == Y_STATE_WAIT_HEADER_ACK ||
        ctx->state == Y_STATE_WAIT_DATA_ACK ||
        ctx->state == Y_STATE_WAIT_EOT_NAK ||
        ctx->state == Y_STATE_WAIT_EOT_ACK ||
        ctx->state == Y_STATE_WAIT_FINAL_ACK) {
        y_send_frame(ctx);
    }
}

void ymodem_sender_cancel(ymodem_sender_t *ctx)
{
    uint8 can[2] = {Y_CAN, Y_CAN};
    if (ctx && ctx->result == YMODEM_RESULT_RUNNING) {
        if (ctx->send) ctx->send(ctx->priv, can, sizeof(can));
        y_fail(ctx, "cancelled locally");
    }
}

ymodem_result_t ymodem_sender_result(const ymodem_sender_t *ctx)
{
    return ctx ? ctx->result : YMODEM_RESULT_ERROR;
}

const char *ymodem_sender_error(const ymodem_sender_t *ctx)
{
    return (ctx && ctx->error) ? ctx->error : "none";
}
