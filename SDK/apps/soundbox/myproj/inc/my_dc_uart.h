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

typedef struct{
    uint8 bat_percent;            // 电量百分比
    uint16 remain_time;           // 剩余可用时间(单位min)
    uint16 bat_temp;              // 电池温度(单位0.1℃)
    uint16 output_total_power;    // 输出总功率(单位0.1W)
    uint16 input_total_power;     // 输入总功率(单位0.1W)
    uint16 ac_power;              // AC功率(单位0.1W)
    uint16 dc_power;              // DC功率(单位0.1W)
    uint16 usb_power;             // USB功率(单位0.1W)
    uint16 led_power;             // LED功率(单位0.1W)
    uint8 sw_status;              // 开关状态(bit0:AC,bit1:DC,bit2:USB,bit3:LED---O:关,1:开)
}device_data;

void my_dc_uart_init(void);
void my_dc_uart_deinit(void);

unsigned int my_dc_uart_send(unsigned char *data, unsigned int size);

void my_dc_proto_feed(const unsigned char *data, unsigned int len);

void my_dc_uart_task(void *p_arg);

#endif

