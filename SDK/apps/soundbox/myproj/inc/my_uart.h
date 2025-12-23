/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : my_uart.h
 * Brief      : UART数据收发模块头文件
 * Version    : V1.0.0
 * Author     : 黄俊凯(huangjunkai@jimiiot.com)
 * Date       : 2025-12-12
************************************************************************************************/

#ifndef MY_UART_H
#define MY_UART_H

#define HAL_UART_1      0 // shell uart

#define MY_SHELL_PORT   HAL_UART_1

typedef void (*my_uart_event_cb_t)(uart_dev uart_num, enum uart_event);

typedef enum
{
    MY_UART_BAUD_9600   =  9600,
    MY_UART_BAUD_115200 =  115200,
    MY_UART_BAUD_921600 =  921600,
} MY_UART_BAUDRATE;

// 系统调试串口使能
#if CONFIG_DEBUG_ENABLE
#define MY_SHELL_UART_TX_PIN    TCFG_DEBUG_UART_TX_PIN
#define MY_SHELL_UART_RX_PIN    TCFG_DEBUG_UART_RX_PIN
#else
#define MY_SHELL_UART_TX_PIN    IO_PORTA_00
#define MY_SHELL_UART_RX_PIN    IO_PORTA_02
#endif

#define MY_UART_RX_BUF_LEN          512
#define MY_UART_DMA_RX_BUF_LEN      1024

typedef struct
{
    module_type mod;        //负责处理此UART read/write事件的模块
    uint16      tx_pin;     // 串口的tx pin脚
    uint16      rx_pin;     // 串口的tx pin脚
    uint32      port;       // 串口的端口号
    uint32      baud;       // 串口的波特率
} MY_UART_ST_STRUCT;

#if CONFIG_DEBUG_ENABLE
extern uint8 debug_dma_init_flag;
#endif

int my_uart_init(const MY_UART_ST_STRUCT *param);

void my_uart_deinit(const int port);

int32 my_shell_uart_read_data(uint8 *buff, uint32 buff_len);

uint32 my_uart_write_data(const int port, uint8 *data, uint32 size);

#endif