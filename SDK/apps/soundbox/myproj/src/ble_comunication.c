/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : ble_comunication.c
 * Brief      : 设备蓝牙交互模块
 * Version    : V1.0.0
 * Author     : 周森达(zhousenda@jimiiot.com)
 * Date       : 2026-01-14
************************************************************************************************/
#include "my_common.h"
#include "mbedtls/aes.h"

static uint8  rx_ble_buf[BLE_SVC_RX_MAX_LEN];
static uint16 rx_ble_buf_index = 0;

uint16 ble_server_mtu = BLE_SERVER_MTU_DFT;

static uint16 prva_a;
static uint8 aes_base_key[16] = {0x3A, 0x60, 0x43, 0x2A, 0x5C, 0x01, 0x21, 0x1F,
                                 0x29, 0x1E, 0x0F, 0x4E, 0x0C, 0x13, 0x28, 0x25};

/* PKCS7 填充 */
uint32 pkcs7_pad(uint8 *buf, uint32 len, uint32 block_size)
{
    uint8 pad_byte;
    uint32 i;
    uint32 pad_len = block_size - (len % block_size);

    if (buf == NULL || len == 0 || block_size == 0)
    {
        return 0;
    }

    pad_byte = (uint8)pad_len;
    for (i = 0; i < pad_len; i++)
    {
        buf[len + i] = pad_byte;
    }

    return len + pad_len;
}

/* PKCS7 去填充 */
uint32 pkcs7_unpad(uint8 *buf, uint32 len)
{
    uint8 pad_byte;
    uint32 i;

    if (buf == NULL || len == 0) return 0;

    pad_byte = buf[len - 1];
    if (pad_byte > len)
    {
        return len;  // 无效填充
    }

    // 验证填充字节
    for (i = 0; i < pad_byte; i++)
    {
        if (buf[len - 1 - i] != pad_byte)
        {
            return len;  // 无效填充
        }
    }

    return len - pad_byte;
}

/* ECB 模式加密 */
int aes_ecb_encrypt(const uint8 *key, int keybits,
                    const uint8 *plaintext, uint32 pt_len,
                    uint8 *ciphertext, uint32 ct_len)
{
    int ret;
    mbedtls_aes_context ctx;
    uint32 padded_len;
    uint8 *padded_input = NULL;
    uint32 i;

    if (key == NULL || plaintext == NULL || ciphertext == NULL)
    {
        return -1;
    }

    // 1. 初始化上下文
    mbedtls_aes_init(&ctx);

    // 2. 设置加密密钥
    ret = mbedtls_aes_setkey_enc(&ctx, key, keybits);
    if (ret != 0)
    {
        my_log_printf(1, "aes_setkey_enc fail: 0x%04x", -ret);
        goto cleanup;
    }

    // 3. 填充数据
    padded_input = malloc(pt_len + 16);
    if (!padded_input)
    {
        ret = MBEDTLS_ERR_AES_INVALID_INPUT_LENGTH;
        goto cleanup;
    }

    memcpy(padded_input, plaintext, pt_len);
    padded_len = pkcs7_pad(padded_input, pt_len, 16);
    if (ct_len < padded_len || padded_len == 0)
    {
        my_log_printf(1, "ciphertext data too small or padded_len is 0");
        goto cleanup;
    }

    // 4. 加密每个块
    for (i = 0; i < padded_len; i += 16)
    {
        ret = mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT,
                                   padded_input + i, ciphertext + i);
        if (ret != 0)
        {
            my_log_printf(1, "aes_crypt_ecb fail: 0x%04x", -ret);
            break;
        }
    }

    if (ret == 0)
    {
        my_log_printf(1, "ECB Encryption success, padded_len:%d", padded_len);

        my_log_printf(1, "key:");
        put_buf((u8 *)key, keybits / 8);

        my_log_printf(1, "plaintext:");
        put_buf((u8 *)plaintext, pt_len);

        my_log_printf(1, "ciphertext:");
        put_buf((u8 *)ciphertext, padded_len);
    }

    free(padded_input);

cleanup:
    // 5. 清理上下文
    mbedtls_aes_free(&ctx);
    return ret;
}

/* ECB 模式解密 */
int aes_ecb_decrypt(const uint8 *key, int keybits,
                    const uint8 *ciphertext, uint32 ct_len,
                    uint8 *plaintext, uint32 pt_len)
{
    int ret = 0;
    mbedtls_aes_context ctx;
    uint32 i;
    uint32 unpad_len;

    if (key == NULL || ciphertext == NULL || plaintext == NULL)
    {
        return -1;
    }

    // 1. 初始化上下文
    mbedtls_aes_init(&ctx);

    // 2. 设置解密密钥
    ret = mbedtls_aes_setkey_dec(&ctx, key, keybits);
    if (ret != 0)
    {
        my_log_printf(1, "aes_setkey_dec fail: 0x%04x", -ret);
        goto cleanup;
    }

    // 3. 解密每个块
    for (i = 0; i < ct_len; i += 16)
    {
        ret = mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_DECRYPT,
                                   ciphertext + i, plaintext + i);
        if (ret != 0)
        {
            my_log_printf(1, "aes_crypt_ecb fail: 0x%04x", -ret);
            break;
        }
    }

    if (ret == 0)
    {
        // 4. 去除填充
        unpad_len = pkcs7_unpad(plaintext, ct_len);
        my_log_printf(1, "Decryption successful, unpad_len:%d", unpad_len);

        my_log_printf(1, "key:");
        put_buf((u8 *)key, keybits / 8);

        my_log_printf(1, "ciphertext:");
        put_buf((u8 *)ciphertext, ct_len);

        my_log_printf(1, "plaintext:");
        put_buf((u8 *)plaintext, ct_len);
    }

cleanup:
    // 5. 清理上下文
    mbedtls_aes_free(&ctx);
    return ret;
}

/************************************************************************
**@brief: 蓝牙数据发送
**@param[in] data:  输入数据指针
**@param[in] len:   输入数据长度
*************************************************************************/
void BLE_DataTransOverBle(uint8 *data, uint16 len)
{
    my_log_printf(1, "len=%d,uart_trans_ble:", len);
    put_buf((u8 *)data, len);

    ble_server_send_notification(data, len);
}

static uint32 ble_comu_generate_pkey(void)
{
    uint32 prva_key;

    prva_a = rand32() & 0xff;
    prva_a <<= 8;
    prva_a |= rand32() & 0xff;

    prva_key = prva_a;
    prva_key *= my_param_get_Gvalue();
    return(prva_key);
}

/************************************************************************
**@brief: 蓝牙数据加密组包
**@param[in] type:  发送数据类型
**@param[in] data:  发送的数据
**@param[in] len:   发送的数据长度
*************************************************************************/
static void ble_comu_send_packet(uint16 type, uint8 *data, uint16 len)
{
    uint8 tx_ble_buf[BLE_RESP_LENGTH_MAX];
    uint8 encrypt_out[BLE_RESP_LENGTH_MAX];
    int ret;

    if((len > BLE_RESP_LENGTH_MAX) || (len % BLE_CMD_DATA_LEN_UNIT))
    {
        my_log_printf(1, "ble packet len(%d) error!", len);
        return;
    }

    // 封装包头、包类型
    tx_ble_buf[0] = (BLE_DATA_PACKET_HEAD >> 8) & 0xFF;
    tx_ble_buf[1] = BLE_DATA_PACKET_HEAD & 0xFF;
    tx_ble_buf[2] = (type >> 8) & 0xFF;
    tx_ble_buf[3] = type & 0xFF;

    memcpy(&tx_ble_buf[4], data, len);
    my_log_printf(1, "tx_ble:");
    put_buf((u8 *)tx_ble_buf, len + 4);

    // 密钥交换不需要加密
    if(type == BLE_DATA_TYPE_PKEY)
    {
        memcpy(encrypt_out, data, len);
    }
    else
    {
        /* AES-128-ECB 加密 */
        ret = aes_ecb_encrypt(aes_base_key, 128, data, len,
                             encrypt_out, sizeof(encrypt_out));
        if (ret != 0) 
        {
            my_log_printf(1, "aes_ecb_encrypt! ret=%d", ret);
            return;
        }
    }

    memcpy(&tx_ble_buf[4], encrypt_out, len);
    BLE_DataTransOverBle(tx_ble_buf, len + 4);
}

static void ble_comu_key_data_handle(const uint8 *data, uint16 len)
{
    uint64 temp = 0;
    uint32 prva_h, rx_key = 0;
    uint32 rx_pkey_cmd = 0;
    uint8  i, out_data[16];

    for(i = 0; i < 4; i++)
    {
        rx_pkey_cmd <<= 8;
        rx_pkey_cmd |= *(data + i);
    }
    if(rx_pkey_cmd != BLE_PKEY_RX_CMD)
    {
        my_log_printf(1, "ble_rx_pkey_cmd error cmd=%08x!",rx_pkey_cmd);
        return;
    }

    for(i = 4; i < 8; i++)
    {
        rx_key <<= 8;
        rx_key |= *(data + i);
    }

    prva_h = ble_comu_generate_pkey();      // 获取随机数并产生公钥
    temp = rx_key;
    temp *= prva_a;                         // 拿到对方的公钥*自己的随机数

    aes_base_key[15] = (temp & 0xFF);
    aes_base_key[14] = (temp >> 8) & 0xFF;
    aes_base_key[13] = (temp >> 16) & 0xFF;
    aes_base_key[12] = (temp >> 24) & 0xFF;
    aes_base_key[11] = (temp >> 32) & 0xFF;
    aes_base_key[10] = (temp >> 40) & 0xFF;

    out_data[0] = BLE_COMU_CMD_START;
    out_data[1] = BLE_PKEY_RSP_DATA1;
    out_data[2] = BLE_COMU_DEV_CODE;
    out_data[3] = BLE_PKEY_RSP_DATA3;

    out_data[4] = (prva_h >> 24) & 0xFF;
    out_data[5] = (prva_h >> 16) & 0xFF;
    out_data[6] = (prva_h >> 8) & 0xFF;
    out_data[7] = prva_h & 0xFF;

    for(i = 0; i < 8; i++)
    {
        out_data[8+i] = rand32() & 0xff;
    }

    ble_comu_send_packet(BLE_DATA_TYPE_PKEY, out_data, 16);
    my_log_printf(1, "ble_aes_base_key:");
    put_buf((u8 *)aes_base_key, 16);
}

void ble_comu_response_cmd(uint8 cmd, uint8 param)
{
    uint8 out_data[BLE_CMD_DATA_LEN_UNIT] = {0};
    uint8 i;

    out_data[0] = BLE_COMU_CMD_START;
    out_data[1] = 0x03;                 // len
    out_data[2] = BLE_COMU_DEV_CODE;
    out_data[3] = cmd;
    out_data[4] = param;

    for(i = 5; i < BLE_CMD_DATA_LEN_UNIT; i++)
    {
        out_data[i] = rand32() & 0xff;
    }

    ble_comu_send_packet(BLE_DATA_TYPE_CMD, out_data, BLE_CMD_DATA_LEN_UNIT);
}

static void ble_comu_cid_data_handle(const uint8 *data, uint16 len)
{
    const GsmImei_t *gsmImei = my_param_get_imei();
    if (gsmImei->flag == FLAG_VALID) // IMEI鉴权
    {
        if (memcmp(data, gsmImei->hex, GSM_IMEI_LENGTH) == 0)
        {
            ble_comu_response_cmd(BLE_RSP_CMD_CID, BLE_RSP_PARAM_SUCCESS);
        }
        else
        {
            ble_comu_response_cmd(BLE_RSP_CMD_CID, BLE_RSP_PARAM_FAIL);
            my_log_printf(1, "ble_cid_compare_fail!");
        }
    }
    else
    {
        ble_comu_response_cmd(BLE_RSP_CMD_CID, BLE_RSP_PARAM_FAIL);
        my_log_printf(1, "invalid imei param!");
    }
}

/************************************************************************
**@brief: 组成符合协议规则的应答数据包
**@param[in] type:      应答数据类型
**@param[in] str_data:  应答数据
**@param[in] len:       应答数据长度
*************************************************************************/
void ble_comu_response_or_expansion_cmd(uint16 type, uint8 *str_data, uint8 len)
{
    uint8 out_data[BLE_SVC_RX_MAX_LEN] = {0};
    uint8 send_len = 0;

    memcpy(out_data, str_data, len);
    send_len = len/16;
    send_len *= 16;

    if (len % 16)
        send_len += 16;

    ble_comu_send_packet(type, out_data, send_len);
}

/************************************************************************
**@brief: 解析并处理对应的AT指令函数并发送应答数据
**@param[in] data:  接收到的数据
**@param[in] len:   接收到的数据长度
*************************************************************************/
static void ble_comu_at_cmd_handle(const uint8 *data, uint16 len)
{
    at_cmd_struc ble_at_msg = {0};
    uint16 cmd_type = 0;

#if 0
    my_log_printf(1, "ble_comu_at_cmd_handle:%s, len=%d", data, len);

    my_log_printf(1, "hex data:");
    put_buf(data, len);
#endif

    ble_at_msg.rcv_length = len;
    memcpy(ble_at_msg.rcv_msg, data, len);

    cmd_type = at_recv_cmd_handler(&ble_at_msg);

    // BLE_SERVER_MAX_DATA_LEN - 4是因为包头有四个字节的长度
    if(ble_at_msg.resp_length > 0 && ble_at_msg.resp_length <= (BLE_SERVER_MAX_DATA_LEN - 4))
    {
        ble_comu_response_or_expansion_cmd(cmd_type, (uint8*)ble_at_msg.resp_msg, ble_at_msg.resp_length);
    }
}

/************************************************************************
**@brief: APP下发的数据包，具体数据类型处理
**@param[in] type:  收到的数据类型
**@param[in] data:  收到的数据
**@param[in] len:   收到的数据长度
*************************************************************************/
void ble_comu_app_handle(uint32 type, const uint8 *data, uint16 len)
{
    int ret;
    uint8 dec_buf[BLE_SVC_RX_MAX_LEN - 4] = {0};
    uint8 rsp_buf[BLE_SVC_RX_MAX_LEN - 4] = {0};

    if(type == BLE_DATA_TYPE_PKEY)
    {
        // 公钥数据
        ble_comu_key_data_handle(data, len);
    }
    else
    {
        if ((len % 16) != 0 || len > (BLE_SVC_RX_MAX_LEN - 4))
        {
            my_log_printf(1, "ble data length error(%d)!",len);
        }
        else
        {
            /* AES-128-ECB 解密 */
            ret = aes_ecb_decrypt(aes_base_key, 128,
                                data, len,
                                dec_buf, sizeof(dec_buf));
            if (ret != 0) 
            {
                my_log_printf(1, "aes_ecb_decrypt error!");
                return;
            }

            my_log_printf(1, "rx_ble:%x", type);

            my_log_printf(1, "original data:");
            put_buf((u8 *)data, len);
            my_log_printf(1, "ble_aes_base_key:");
            put_buf((u8 *)aes_base_key, 16);
            my_log_printf(1, "ble_decrypt:");
            put_buf((u8 *)dec_buf, len);

            switch(type)
            {
                case BLE_DATA_TYPE_CID:         //串号数据
                {
                    // received CID data packet
                    ble_comu_cid_data_handle(dec_buf, len);
                }
                break;

                case BLE_DATA_TYPE_AT_CMD:      //用户指令
                {
                    ble_comu_at_cmd_handle(dec_buf, len);
                }
                break;

                case BLE_DATA_TYPE_EXPANSION_MODULE:    //扩展模块的应答,无需处理
                {
                    my_log_printf(1, "rev expansion response");
                }
                break;

                default:
                {
                    my_log_printf(1, "ble packet type error!");
                }
                break;
            }
        }
    }
}

/************************************************************************
**@brief: app下发的通用数据解析(解析包头和包类型)
**@param[in] data:  收到的数据
**@param[in] len:   收到的数据长度
*************************************************************************/
void ble_app_comm_data_proc(const uint8 *data, uint16 len)
{
    uint16 pkt_head, pkt_type;

    pkt_head = *(data + 0);
    pkt_head <<= 8;
    pkt_head |= *(data + 1);

    pkt_type = *(data + 2);
    pkt_type <<= 8;
    pkt_type |= *(data + 3);

    if(pkt_head != BLE_DATA_PACKET_HEAD)
    {
        my_log_printf(1, "ble packet head(0x%04x) error!", pkt_head);
    }
    else
    {
        my_log_printf(1, "ble packet type:0x%04x,len=%d", pkt_type, len);
        ble_comu_app_handle(pkt_type, data + 4, len - 4);
    }
}

/************************************************************************
**@brief: 蓝牙接收数据缓存并通过消息发送给蓝牙线程进行消息处理
**@param[in] data:  收到的数据
**@param[in] len:   收到的数据长度
*************************************************************************/
void BLE_DataInputBuffer(const uint8* data, uint16 len)
{
    if (rx_ble_buf_index == 0 && len <= BLE_SERVER_MAX_DATA_LEN && len >= BLE_GATT_FRAME_LEN_MIN)     // 长度检查
    {
        memcpy(&rx_ble_buf[0], data, len);
        rx_ble_buf_index = len;

        my_log_printf(1, "BLE_DataInput:");
        put_buf((u8 *)rx_ble_buf, BLE_GATT_FRAME_LEN_MIN);
        my_send_msg(MOD_MAIN, MOD_BLE, MY_MSG_BLE_RX);
    }
}

void ble_rx_proc_handle(void)
{
    uint16 read_packet_head = (rx_ble_buf[0] << 8) | rx_ble_buf[1];
    uint16 read_cmd_head = (rx_ble_buf[2] << 8) | rx_ble_buf[3];

    if(read_packet_head == BLE_DATA_PACKET_HEAD)
    {
        if(read_cmd_head == BLE_DATA_TYPE_FILE_TRANS)
        {
            // TODO
        }
        else
        {
            ble_app_comm_data_proc(rx_ble_buf, rx_ble_buf_index);   // 指令处理
        }
    }
    else
    {
        my_log_printf(1, "ble packet head(0x%04x) error!", read_packet_head);
    }

    rx_ble_buf_index = 0;
}

