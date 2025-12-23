/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : my_uart.c
 * Brief      : UART数据收发处理模块
 * Version    : V1.0.0
 * Author     : 黄俊凯(huangjunkai@jimiiot.com)
 * Date       : 2025-12-12
************************************************************************************************/

#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".my_uart.data.bss")
#pragma data_seg(".my_uart.data")
#pragma const_seg(".my_uart.text.const")
#pragma code_seg(".my_uart.text")
#endif

#include "my_common.h"

module_type g_uart_owner[3] = {0};    // 串口的拥有者，即串口消息的接收者

void *g_uart_dma_buf[3] = {NULL};   // 串口dma内存地址

#if CONFIG_DEBUG_ENABLE
/**
 * SDK默认调试串口dma初始化完成标志
 * 在debug_uart_init中初始化dma会失败，
 * 放到my_uart_init中进行。
 */
uint8 debug_dma_init_flag = 0;
#endif

/************************************************************************
**@brief: 串口读写事件回调
**@param[in] uart_num: 端口号
**@param[in] event: 回调事件
*************************************************************************/
//void (*irq_callback)(uart_dev uart_num, enum uart_event);
static void handle_uart1_event(int uart_num, enum uart_event event)
{
    // 数据帧接收完成或接收缓冲区溢出
    if ((event & UART_EVENT_RX_TIMEOUT) || 
        (event & UART_EVENT_RX_FIFO_OVF))
    {
        my_send_msg(MOD_MAIN, g_uart_owner[HAL_UART_1], MY_MSG_UART_RECV);
    }
}

/************************************************************************
**@brief: 串口初始化
**@param[in] param: 初始化数据信息
**@return: 初始化状态，负数表示失败
*************************************************************************/
int my_uart_init(const MY_UART_ST_STRUCT *param)
{
    int ret = 0;
    uint32 dma_buff_len = 0;
    my_uart_event_cb_t uart_event_cb = NULL;

    g_uart_owner[param->port] = param->mod;

    // uart config init
    const struct uart_config config = {
        .baud_rate = param->baud,
        .tx_pin = param->tx_pin,
        .rx_pin = param->rx_pin,
        .parity = UART_PARITY_DISABLE,
        .tx_wait_mutex = 0,//1:不支持中断调用,互斥,0:支持中断,不互斥
    };

    // 如果开启了SDK的调试串口，系统启动时默认会初始化调试串口
#if CONFIG_DEBUG_ENABLE
    if (param->port != HAL_UART_1)
#endif
    {
        ret = uart_init(param->port, &config);
        if (ret < 0) {
            return ret;
        }
    }

    if (param->port == HAL_UART_1) 
    {
        uart_event_cb = handle_uart1_event;
        dma_buff_len = MY_UART_DMA_RX_BUF_LEN;
    }

    // dma config init
    g_uart_dma_buf[param->port] = dma_malloc(dma_buff_len);
    memset(g_uart_dma_buf[param->port], 0, dma_buff_len);

    const struct uart_dma_config dma = {
        .rx_timeout_thresh = 3 * 10000000 / config.baud_rate, // 单位:us,公式：3*10000000/baud(ot:3个byte时间)
                                                              // 接收数据不够frame_size，串口空闲3个byte时间就触发中断
        .event_mask = UART_EVENT_RX_FIFO_OVF | UART_EVENT_RX_TIMEOUT,
        .irq_priority = 3,
        .irq_callback = uart_event_cb,
        .rx_cbuffer = g_uart_dma_buf[param->port],
        .rx_cbuffer_size = dma_buff_len,
        .frame_size = dma_buff_len,// 配置接收多少字节后触发中断(一般=rx_cbuffer_size)
    };

    ret = uart_dma_init(param->port, &dma);
    if (ret < 0)
    {
        my_uart_deinit(param->port);
        return ret;
    }

#if CONFIG_DEBUG_ENABLE
    if (param->port == HAL_UART_1) {
        debug_dma_init_flag = 1;
    }
#endif

    // 打印uart配置信息
    uart_dump();

    return ret;
}

/************************************************************************
**@brief: 串口去初始化
**@param[in] port: 端口号
*************************************************************************/
void my_uart_deinit(const int port)
{
#if CONFIG_DEBUG_ENABLE
    if (port == HAL_UART_1) {
        debug_dma_init_flag = 0;
    }
#endif

    if (NULL != g_uart_dma_buf[port])
    {
        dma_free(g_uart_dma_buf[port]);
        g_uart_dma_buf[port] = NULL;
    }

    uart_deinit(port);
}

/************************************************************************
**@brief: 读取shell缓冲区中的数据
**@param[out] buff: 数据缓冲区
**@param[in] buff_len: 数据缓冲区长度
**@return: 实际读取的数据长度，负数表示错误
*************************************************************************/
int32 my_shell_uart_read_data(uint8 *buff, uint32 buff_len)
{
    int32 len = 0;

    len = uart_recv_bytes(MY_SHELL_PORT, (void *)buff, buff_len);

    return len;
}

/************************************************************************
**@brief: 向串口写入数据
**@param[in] port: 端口号
**@param[in] data: 数据缓冲区信息
**@param[in] size: 待写入数据的长度信息
**@return: 实际写入的数据长度，0表示错误
*************************************************************************/
uint32 my_uart_write_data(const int port, uint8 *data, uint32 size)
{
    int32 bytesWrite = 0;

    bytesWrite = uart_send_bytes(port, (const void *)data, size);
    if (bytesWrite != size)
    {
        my_log_printf(1, "uart write return failed! port[%d]\r\n", port);
        return 0;
    }
    // 等待串口发送完成
    uart_wait_tx_idle(port, 0);

    return (uint32)bytesWrite;
}

