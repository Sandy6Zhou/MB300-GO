/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : my_dc_uart.h
 * Brief      : DC板UART通信模块头文件
 * Version    : V1.0.0
 * Author     : songshiqing(songshiqing@jimiiot.com)
 * Date       : 2026-03-02
************************************************************************************************/

#ifndef MY_DC_UART_H
#define MY_DC_UART_H

#define MY_DC_UART_PORT              HAL_UART_2
#define MY_DC_UART_BAUD              MY_UART_BAUD_115200

#define MY_DC_UART_TX_PIN            IO_PORTA_07
#define MY_DC_UART_RX_PIN            IO_PORTA_08

#define MY_DC_UART_RX_TMP_BUF_LEN    512

void my_dc_uart_init(void);
void my_dc_uart_deinit(void);

unsigned int my_dc_uart_send(unsigned char *data, unsigned int size);

void my_dc_proto_feed(const unsigned char *data, unsigned int len);

void my_dc_uart_task(void *p_arg);

#endif

