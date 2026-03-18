/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : my_log.h
 * Brief      : 设备日志打印模块头文件
 * Version    : V1.0.0
 * Author     : 黄俊凯(huangjunkai@jimiiot.com)
 * Date       : 2025-12-12
************************************************************************************************/

#ifndef MY_LOG_H
#define MY_LOG_H

#define UART_LOG_MAX_LEN    256

#define my_log_printf my_log_printf_internal

void my_log_printf_internal(int log_level, char *fmt, ...);
void my_log_mutex_init(void);

#endif