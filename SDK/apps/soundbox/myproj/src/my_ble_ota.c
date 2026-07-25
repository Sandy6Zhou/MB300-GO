/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : my_ble_ota.c
 * Brief      : 通过BLE协议接收DC固件并暂存到DCOTA预留区
 * Version    : V1.0.0
 * Author     : songshiqing(songshiqing@jimiiot.com)
 * Date       : 2026-07-16
 ************************************************************************************************/

#include "my_common.h"
#include "utils/fs/resfile.h"
#include "asm/sfc_norflash_api.h"
#include "mbedtls/md5.h"

/*
 * BLE DC OTA流程：
 * 1) APP通过0x4605/0x01请求开始传输，设备确认当前可接收文件；
 * 2) APP通过0x4605/0x02下发文件长度及MD5，设备擦除DCOTA预留区；
 * 3) 设备通过0x4605/0x03按MTU请求分片，校验CRC后写入并回读Flash；
 * 4) 文件接收完成后按有效长度校验MD5，通过0x4605/0x04返回结果；
 * 5) APP确认结束后，在DC UART任务中启动YMODEM升级DC。
 *
 * 0x4605解析、Flash擦写及控制包发送均在BLE任务执行；DC YMODEM状态机在DC UART任务执行。
 */

/* ========== 0x4605文件传输协议常量 ========== */
#define BLE_OTA_SECOND_HEAD_0      0x78
#define BLE_OTA_SECOND_HEAD_1      0x79
#define BLE_OTA_APP_CODE           0xC2
#define BLE_OTA_DEVICE_CODE        0xB3
#define BLE_OTA_CMD_START          0x01
#define BLE_OTA_CMD_FILE_INFO      0x02
#define BLE_OTA_CMD_FRAGMENT       0x03
#define BLE_OTA_CMD_FINISH         0x04
#define BLE_OTA_RESULT_OK          0x00
#define BLE_OTA_RESULT_ERROR       0x01
#define BLE_OTA_RESULT_MD5_ERROR   0x02
#define BLE_OTA_RESULT_TOO_LARGE   0x03
#define BLE_OTA_RESULT_TIMEOUT     0x04
#define BLE_OTA_TIMEOUT_MS         15000u
#define BLE_OTA_TICK_MS            1000u
#define BLE_OTA_MAX_FRAGMENT       480u
#define BLE_OTA_FRAGMENT_RETRY_MAX 5u
#define BLE_OTA_CRC_INIT           0x0000u

/* BLE文件接收状态。 */
typedef enum
{
    BLE_OTA_IDLE = 0,        /* 空闲，可接受新的0x01开始请求 */
    BLE_OTA_WAIT_FILE_INFO,  /* 已确认开始，等待APP下发文件长度和MD5 */
    BLE_OTA_RECEIVING,       /* 正在按偏移请求、校验并写入文件分片 */
    BLE_OTA_WAIT_FINISH_ACK, /* 已返回接收结果，等待APP发送0x04确认 */
} my_ble_ota_state_t;

/* 单次BLE文件接收上下文。 */
typedef struct
{
    my_ble_ota_state_t state; /* 当前文件接收状态 */
    RESFILE *file;            /* DCOTA预留区资源句柄，仅用于定位及后续DC读取 */
    uint32 flash_addr;        /* DCOTA预留区对应的物理Flash地址 */
    uint32 file_size;         /* APP声明的固件有效长度 */
    uint32 offset;            /* 下一次请求及写入的文件偏移 */
    uint32 request_len;       /* 当前请求的分片长度，最后一包仍按此长度补FF */
    uint32 idle_ms;           /* 距离上次有效协议进展的时间 */
    uint8 expected_md5[16];   /* APP下发的完整文件MD5 */
    uint8 finish_result;      /* 已回复给APP的0x04接收结果 */
    uint8 fragment_retry;     /* 当前偏移的错误重试次数 */
    uint8 io_buffer[512];     /* Flash回读及MD5计算复用缓冲区 */
} my_ble_ota_ctx_t;

static my_ble_ota_ctx_t g_ble_ota; /* BLE OTA唯一会话 */

/** @brief 从协议缓冲区读取小端uint16。 */
static uint16 my_ble_ota_get_le16(const uint8 *data)
{
    return (uint16)data[0] | ((uint16)data[1] << 8);
}

/** @brief 从协议缓冲区读取小端uint32。 */
static uint32 my_ble_ota_get_le32(const uint8 *data)
{
    return (uint32)data[0] | ((uint32)data[1] << 8) |
           ((uint32)data[2] << 16) | ((uint32)data[3] << 24);
}

/** @brief 将uint16按小端格式写入协议缓冲区。 */
static void my_ble_ota_put_le16(uint8 *data, uint16 value)
{
    data[0] = (uint8)value;
    data[1] = (uint8)(value >> 8);
}

/** @brief 将uint32按小端格式写入协议缓冲区。 */
static void my_ble_ota_put_le32(uint8 *data, uint32 value)
{
    data[0] = (uint8)value;
    data[1] = (uint8)(value >> 8);
    data[2] = (uint8)(value >> 16);
    data[3] = (uint8)(value >> 24);
}

/** @brief 计算协议附录CRC16，算法0xA001，初值0x0000。 */
static uint16 my_ble_ota_crc16(const uint8 *data, uint32 len)
{
    uint16 crc = BLE_OTA_CRC_INIT;
    uint32 i;
    uint8 bit;

    for (i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++)
        {
            crc = (crc & 1u) ? (uint16)((crc >> 1) ^ 0xA001u) : (uint16)(crc >> 1);
        }
    }
    return crc;
}

/** @brief 关闭DCOTA资源句柄。 */
static void my_ble_ota_close_file(void)
{
    if (g_ble_ota.file != NULL)
    {
        resfile_close(g_ble_ota.file);
        g_ble_ota.file = NULL;
    }
}

/** @brief 停止定时器并清理当前BLE文件接收会话。 */
static void my_ble_ota_stop_session(void)
{
    my_stop_timer(MY_TIMER_BLE_OTA);
    my_ble_ota_close_file();
    g_ble_ota.state = BLE_OTA_IDLE;
    g_ble_ota.file_size = 0;
    g_ble_ota.flash_addr = 0;
    g_ble_ota.offset = 0;
    g_ble_ota.request_len = 0;
    g_ble_ota.idle_ms = 0;
}

/** @brief BLE OTA定时器回调，仅向BLE任务投递超时tick消息。 */
static void my_ble_ota_timer_cb(void *param)
{
    (void)param;
    my_send_msg(MOD_BLE, MOD_BLE, MY_MSG_BLE_OTA_TICK);
}

/** @brief 发送0x4605控制包，包括开始应答和文件接收结果。 */
static int my_ble_ota_send_control(uint8 cmd, uint8 result)
{
    uint8 out[16] = {0};

    out[0] = BLE_OTA_SECOND_HEAD_0;
    out[1] = BLE_OTA_SECOND_HEAD_1;
    out[2] = BLE_OTA_DEVICE_CODE;
    out[3] = cmd;
    my_ble_ota_put_le16(&out[4], 10);
    if (cmd == BLE_OTA_CMD_FINISH)
    {
        out[6] = result;
    }
    return ble_comu_send_packet(BLE_DATA_TYPE_FILE_TRANS, out, sizeof(out));
}

/** @brief 保存并向APP回复本次文件接收结果，随后等待0x04确认。 */
static void my_ble_ota_send_finish(uint8 result)
{
    g_ble_ota.finish_result = result;
    g_ble_ota.state = BLE_OTA_WAIT_FINISH_ACK;
    g_ble_ota.idle_ms = 0;
    (void)my_ble_ota_send_control(BLE_OTA_CMD_FINISH, result);
    my_log_printf(1, "[BLE_OTA] receive finish result=%u size=%u",
                  result, (unsigned)g_ble_ota.offset);
}

/** @brief 根据当前ATT有效长度选择16字节对齐的文件分片长度。 */
static uint32 my_ble_ota_select_fragment_len(void)
{
    uint32 att_len = BLE_SERVER_MAX_DATA_LEN;
    uint32 fragment_len;

    /* Complete 0x4605 response is: outer header 4 + encrypted inner overhead 16 + data. */
    if (att_len <= 20u)
    {
        return 0;
    }
    fragment_len = (att_len - 20u) & ~15u;
    if (fragment_len > BLE_OTA_MAX_FRAGMENT)
    {
        fragment_len = BLE_OTA_MAX_FRAGMENT;
    }
    return fragment_len;
}

/** @brief 请求APP发送当前offset对应的文件分片。 */
static int my_ble_ota_request_fragment(void)
{
    uint8 out[16] = {0};
    uint32 fragment_len = my_ble_ota_select_fragment_len();

    if (fragment_len == 0)
    {
        my_log_printf(1, "[BLE_OTA] MTU too small: %u", (unsigned)ble_server_mtu);
        return -1;
    }

    g_ble_ota.request_len = fragment_len;
    out[0] = BLE_OTA_SECOND_HEAD_0;
    out[1] = BLE_OTA_SECOND_HEAD_1;
    out[2] = BLE_OTA_DEVICE_CODE;
    out[3] = BLE_OTA_CMD_FRAGMENT;
    my_ble_ota_put_le16(&out[4], 10);
    my_ble_ota_put_le32(&out[6], g_ble_ota.offset);
    my_ble_ota_put_le32(&out[10], fragment_len);
    return ble_comu_send_packet(BLE_DATA_TYPE_FILE_TRANS, out, sizeof(out));
}

/** @brief 重试当前分片；超过上限后结束本次文件传输。 */
static void my_ble_ota_retry_fragment(void)
{
    g_ble_ota.fragment_retry++;
    if (g_ble_ota.fragment_retry > BLE_OTA_FRAGMENT_RETRY_MAX)
    {
        my_log_printf(1, "[BLE_OTA] fragment retry exhausted off=%u",
                      (unsigned)g_ble_ota.offset);
        my_ble_ota_send_finish(BLE_OTA_RESULT_ERROR);
        return;
    }

    if (my_ble_ota_request_fragment() != 0)
    {
        my_ble_ota_send_finish(BLE_OTA_RESULT_ERROR);
    }
}

/**
 * @brief 打开并擦除DCOTA预留区。
 * @return 0=成功，-1=资源不存在、容量不足或Flash擦除失败。
 */
static int my_ble_ota_prepare_storage(void)
{
    struct resfile_attrs attrs = {0};
    uint32 erase_offset;

    my_ble_ota_close_file();
    g_ble_ota.file = resfile_open(MY_BLE_OTA_STORAGE_PATH);
    if (g_ble_ota.file == NULL ||
        resfile_get_len(g_ble_ota.file) < (int)MY_BLE_OTA_STORAGE_SIZE ||
        resfile_get_attrs(g_ble_ota.file, &attrs) < 0)
    {
        my_log_printf(1, "[BLE_OTA] open storage failed: %s", MY_BLE_OTA_STORAGE_PATH);
        my_ble_ota_close_file();
        return -1;
    }

    g_ble_ota.flash_addr = sdfile_cpu_addr2flash_addr(attrs.sclust);
    for (erase_offset = 0; erase_offset < MY_BLE_OTA_STORAGE_SIZE; erase_offset += 0x1000u)
    {
        if (norflash_ioctl(NULL, IOCTL_ERASE_SECTOR,
                           g_ble_ota.flash_addr + erase_offset) != 0)
        {
            my_log_printf(1, "[BLE_OTA] erase storage failed addr=0x%x",
                          (unsigned)(g_ble_ota.flash_addr + erase_offset));
            my_ble_ota_close_file();
            g_ble_ota.flash_addr = 0;
            return -1;
        }
    }
    my_log_printf(1, "[BLE_OTA] storage ready addr=0x%x size=%u",
                  (unsigned)g_ble_ota.flash_addr,
                  (unsigned)MY_BLE_OTA_STORAGE_SIZE);
    return 0;
}

/** @brief 从DCOTA物理地址读取有效文件并校验APP下发的MD5。 */
static int my_ble_ota_check_md5(void)
{
    mbedtls_md5_context md5;
    uint8 digest[16];
    uint32 remain = g_ble_ota.file_size;
    uint32 read_len;
    int ret = -1;

    if (g_ble_ota.flash_addr == 0)
    {
        return -1;
    }

    mbedtls_md5_init(&md5);
    if (mbedtls_md5_starts(&md5) != 0)
    {
        goto done;
    }

    while (remain > 0)
    {
        read_len = MIN(remain, (uint32)sizeof(g_ble_ota.io_buffer));
        if (norflash_read(NULL, g_ble_ota.io_buffer, read_len,
                          g_ble_ota.flash_addr + g_ble_ota.file_size - remain) != (int)read_len ||
            mbedtls_md5_update(&md5, g_ble_ota.io_buffer, read_len) != 0)
        {
            goto done;
        }
        remain -= read_len;
    }

    if (mbedtls_md5_finish(&md5, digest) == 0 &&
        memcmp(digest, g_ble_ota.expected_md5, sizeof(digest)) == 0)
    {
        ret = 0;
    }
    else
    {
        my_log_printf(1, "[BLE_OTA] MD5 mismatch");
    }

done:
    mbedtls_md5_free(&md5);
    return ret;
}

/** @brief 处理APP的0x01开始请求并创建新的文件接收会话。 */
static void my_ble_ota_handle_start(void)
{
    if (my_dc_ota_is_active())
    {
        my_log_printf(1, "[BLE_OTA] reject start: DC OTA busy");
        (void)my_ble_ota_send_control(BLE_OTA_CMD_FINISH, BLE_OTA_RESULT_ERROR);
        return;
    }

    my_ble_ota_stop_session();
    memset(&g_ble_ota, 0, sizeof(g_ble_ota));
    g_ble_ota.state = BLE_OTA_WAIT_FILE_INFO;
    if (!my_start_timer(MY_TIMER_BLE_OTA, BLE_OTA_TICK_MS, true, my_ble_ota_timer_cb))
    {
        g_ble_ota.state = BLE_OTA_IDLE;
        return;
    }
    (void)my_ble_ota_send_control(BLE_OTA_CMD_START, 0);
    my_log_printf(1, "[BLE_OTA] start accepted, mtu=%u", (unsigned)ble_server_mtu);
}

/** @brief 解析0x02文件长度和MD5，准备预留区并请求首个分片。 */
static void my_ble_ota_handle_file_info(const uint8 *data, uint16 len)
{
    if (g_ble_ota.state != BLE_OTA_WAIT_FILE_INFO || len < 32)
    {
        return;
    }

    g_ble_ota.file_size = my_ble_ota_get_le32(&data[6]);
    memcpy(g_ble_ota.expected_md5, &data[16], sizeof(g_ble_ota.expected_md5));
    g_ble_ota.idle_ms = 0;
    my_log_printf(1, "[BLE_OTA] file info size=%u limit=%u",
                  (unsigned)g_ble_ota.file_size,
                  (unsigned)MY_BLE_OTA_STORAGE_SIZE);

    if (g_ble_ota.file_size == 0 || g_ble_ota.file_size > MY_BLE_OTA_STORAGE_SIZE)
    {
        my_ble_ota_send_finish(BLE_OTA_RESULT_TOO_LARGE);
        return;
    }
    if (my_ble_ota_select_fragment_len() == 0 || my_ble_ota_prepare_storage() != 0)
    {
        my_ble_ota_send_finish(BLE_OTA_RESULT_ERROR);
        return;
    }

    g_ble_ota.offset = 0;
    g_ble_ota.state = BLE_OTA_RECEIVING;
    if (my_ble_ota_request_fragment() != 0)
    {
        my_ble_ota_send_finish(BLE_OTA_RESULT_ERROR);
    }
}

/**
 * @brief 校验0x03分片的偏移、长度和CRC，写入Flash后立即回读比较。
 * @note 最后一包的CRC覆盖APP补齐的完整分片，但只写入文件有效长度。
 */
static void my_ble_ota_handle_fragment(const uint8 *data, uint16 len)
{
    uint32 offset;
    uint32 fragment_len;
    uint32 valid_len;
    uint16 received_crc;
    uint16 calculated_crc;

    if (g_ble_ota.state != BLE_OTA_RECEIVING || len < 16)
    {
        return;
    }

    offset = my_ble_ota_get_le32(&data[6]);
    fragment_len = my_ble_ota_get_le32(&data[10]);
    if (offset != g_ble_ota.offset || fragment_len != g_ble_ota.request_len ||
        fragment_len > BLE_OTA_MAX_FRAGMENT || len < fragment_len + 16u)
    {
        my_log_printf(1, "[BLE_OTA] fragment mismatch off=%u/%u len=%u/%u",
                      (unsigned)offset, (unsigned)g_ble_ota.offset,
                      (unsigned)fragment_len, (unsigned)g_ble_ota.request_len);
        my_ble_ota_retry_fragment();
        return;
    }

    received_crc = ((uint16)data[14 + fragment_len] << 8) | data[15 + fragment_len];
    calculated_crc = my_ble_ota_crc16(&data[14], fragment_len);
    if (received_crc != calculated_crc)
    {
        my_log_printf(1, "[BLE_OTA] fragment CRC error off=%u rx=%04x calc=%04x",
                      (unsigned)offset, received_crc, calculated_crc);
        my_ble_ota_retry_fragment();
        return;
    }

    valid_len = MIN(fragment_len, g_ble_ota.file_size - g_ble_ota.offset);
    if (norflash_write(NULL, (void *)&data[14], valid_len,
                       g_ble_ota.flash_addr + g_ble_ota.offset) != (int)valid_len ||
        norflash_read(NULL, g_ble_ota.io_buffer, valid_len,
                      g_ble_ota.flash_addr + g_ble_ota.offset) != (int)valid_len ||
        memcmp(g_ble_ota.io_buffer, &data[14], valid_len) != 0)
    {
        my_ble_ota_send_finish(BLE_OTA_RESULT_ERROR);
        return;
    }

    g_ble_ota.fragment_retry = 0;
    g_ble_ota.offset += valid_len;
    g_ble_ota.idle_ms = 0;
    if (g_ble_ota.offset >= g_ble_ota.file_size)
    {
        my_ble_ota_send_finish(my_ble_ota_check_md5() == 0 ? BLE_OTA_RESULT_OK : BLE_OTA_RESULT_MD5_ERROR);
    }
    else if (my_ble_ota_request_fragment() != 0)
    {
        my_ble_ota_send_finish(BLE_OTA_RESULT_ERROR);
    }
}

/** @brief 处理APP的0x04结束确认，成功时转入DC UART/YMODEM升级。 */
static void my_ble_ota_handle_finish_ack(void)
{
    uint32 file_size;
    uint8 result;

    if (g_ble_ota.state != BLE_OTA_WAIT_FINISH_ACK)
    {
        return;
    }

    file_size = g_ble_ota.file_size;
    result = g_ble_ota.finish_result;
    my_ble_ota_stop_session();
    if (result == BLE_OTA_RESULT_OK)
    {
        if (my_dc_ota_request_start_ble(file_size) != 0)
        {
            my_log_printf(1, "[BLE_OTA] DC OTA start request failed");
        }
    }
}

/**
 * @brief 分发解密后的0x4605内部协议包。
 * @param data 从二级头0x78 0x79开始的解密数据。
 * @param len 解密缓冲区有效长度，包含AES补齐内容。
 */
void my_ble_ota_handle_packet(const uint8 *data, uint16 len)
{
    uint16 payload_len;

    if (data == NULL || len < 16 || data[0] != BLE_OTA_SECOND_HEAD_0 ||
        data[1] != BLE_OTA_SECOND_HEAD_1 || data[2] != BLE_OTA_APP_CODE)
    {
        my_log_printf(1, "[BLE_OTA] invalid packet header");
        return;
    }

    payload_len = my_ble_ota_get_le16(&data[4]);
    if ((uint32)payload_len + 6u > len)
    {
        my_log_printf(1, "[BLE_OTA] invalid packet len=%u/%u", payload_len, len);
        return;
    }

    switch (data[3])
    {
        case BLE_OTA_CMD_START:
            my_ble_ota_handle_start();
            break;
        case BLE_OTA_CMD_FILE_INFO:
            my_ble_ota_handle_file_info(data, len);
            break;
        case BLE_OTA_CMD_FRAGMENT:
            my_ble_ota_handle_fragment(data, len);
            break;
        case BLE_OTA_CMD_FINISH:
            my_ble_ota_handle_finish_ack();
            break;
        default:
            my_log_printf(1, "[BLE_OTA] unknown cmd=%u", data[3]);
            break;
    }
}

/** @brief 累计无有效进展时间，超时后向APP返回结果或清理会话。 */
void my_ble_ota_tick(void)
{
    if (g_ble_ota.state == BLE_OTA_IDLE)
    {
        return;
    }

    g_ble_ota.idle_ms += BLE_OTA_TICK_MS;
    if (g_ble_ota.idle_ms >= BLE_OTA_TIMEOUT_MS)
    {
        if (g_ble_ota.state != BLE_OTA_WAIT_FINISH_ACK)
        {
            my_ble_ota_send_finish(BLE_OTA_RESULT_TIMEOUT);
        }
        else
        {
            my_log_printf(1, "[BLE_OTA] finish ACK timeout");
            my_ble_ota_stop_session();
        }
    }
}
