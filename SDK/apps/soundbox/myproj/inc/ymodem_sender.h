#ifndef YMODEM_SENDER_H
#define YMODEM_SENDER_H

/* myproj integer types are declared by my_common.h before this header. */

#define YMODEM_PACKET_1K_SIZE 1024
#define YMODEM_FRAME_MAX_SIZE (YMODEM_PACKET_1K_SIZE + 5)

typedef int (*ymodem_send_cb_t)(void *priv, const uint8 *data, uint32 len);
typedef int (*ymodem_read_cb_t)(void *priv, uint32 offset, uint8 *data, uint32 len);
typedef void (*ymodem_progress_cb_t)(void *priv, uint32 sent, uint32 total);

typedef enum {
    YMODEM_RESULT_IDLE = 0,
    YMODEM_RESULT_RUNNING,
    YMODEM_RESULT_DONE,
    YMODEM_RESULT_ERROR,
} ymodem_result_t;

typedef struct {
    ymodem_send_cb_t send;
    ymodem_read_cb_t read;
    ymodem_progress_cb_t progress;
    void *priv;
    const char *file_name;
    uint32 file_size;

    uint8 frame[YMODEM_FRAME_MAX_SIZE];
    uint16 frame_len;
    uint16 data_len;
    uint8 block_no;
    uint8 state;
    uint8 retries;
    uint8 can_count;
    uint16 wait_ms;
    uint32 offset;
    ymodem_result_t result;
    const char *error;
} ymodem_sender_t;

void ymodem_sender_init(ymodem_sender_t *ctx,
                        const char *file_name,
                        uint32 file_size,
                        ymodem_send_cb_t send_cb,
                        ymodem_read_cb_t read_cb,
                        ymodem_progress_cb_t progress_cb,
                        void *priv);
void ymodem_sender_input(ymodem_sender_t *ctx, const uint8 *data, uint32 len);
void ymodem_sender_tick(ymodem_sender_t *ctx, uint16 elapsed_ms);
void ymodem_sender_cancel(ymodem_sender_t *ctx);
ymodem_result_t ymodem_sender_result(const ymodem_sender_t *ctx);
const char *ymodem_sender_error(const ymodem_sender_t *ctx);

#endif
