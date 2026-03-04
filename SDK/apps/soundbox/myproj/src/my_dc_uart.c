/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : my_dc_uart.c
 * Brief      : DC板UART通信模块
 * Version    : V1.0.0
 * Author     : songshiqing(songshiqing@jimiiot.com)
 * Date       : 2026-03-02
************************************************************************************************/

#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".my_dc_uart.data.bss")
#pragma data_seg(".my_dc_uart.data")
#pragma const_seg(".my_dc_uart.text.const")
#pragma code_seg(".my_dc_uart.text")
#endif

#include "my_common.h"

void my_dc_uart_init(void)
{
    MY_UART_ST_STRUCT param = {
        .mod = MOD_DC_UART,
        .port = MY_DC_UART_PORT,
        .tx_pin = MY_DC_UART_TX_PIN,
        .rx_pin = MY_DC_UART_RX_PIN,
        .baud = MY_DC_UART_BAUD,
    };

    my_uart_init(&param);
    my_log_printf(1, "dc uart init ok. port=%d", MY_DC_UART_PORT);
}

void my_dc_uart_deinit(void)
{
    my_uart_deinit(MY_DC_UART_PORT);
}

uint32 my_dc_uart_send(uint8 *data, uint32 size)
{
    if (data == NULL || size == 0) {
        return 0;
    }

    return my_uart_write_data(MY_DC_UART_PORT, data, size);
}

void my_dc_proto_feed(const uint8 *data, uint32 len)
{
    if (data == NULL || len == 0) {
        return;
    }

    // TODO: 预留协议处理逻辑
    (void)data;
    (void)len;
}

void my_dc_uart_task(void *p_arg)
{
    int ret = 0;
    int msg[8] = {0};
    int32 len = 0;
    uint8 *rx_buff = (uint8 *)dma_malloc(MY_DC_UART_RX_TMP_BUF_LEN + 1);

    (void)p_arg;

    my_dc_uart_init();
    my_log_printf(1, "############# my_dc_uart_task is runing ##################");

    while (1)
    {
        ret = os_taskq_pend(NULL, msg, ARRAY_SIZE(msg));
        if (ret != OS_TASKQ) {
            continue;
        }

        switch (msg[0])
        {
            case MY_MSG_UART_RECV:
            {
                if (rx_buff == NULL) {
                    break;
                }

                memset(rx_buff, 0, MY_DC_UART_RX_TMP_BUF_LEN + 1);
                len = my_dc_uart_read_data(rx_buff, MY_DC_UART_RX_TMP_BUF_LEN);
                if (len > 0)
                {
                    // my_dc_uart_send(rx_buff, (uint32)len); // 临时回环验证：收到什么就回发什么
                    my_dc_proto_feed(rx_buff, (uint32)len);
                }
                break;
            }

            default:
                break;
        }
    }

    if (rx_buff != NULL)
    {
        dma_free(rx_buff);
        rx_buff = NULL;
    }
}

