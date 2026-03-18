/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : my_log.c
 * Brief      : 设备日志打印模块
 * Version    : V1.0.0
 * Author     : 黄俊凯(huangjunkai@jimiiot.com)
 * Date       : 2025-12-12
************************************************************************************************/

#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".my_log.data.bss")
#pragma data_seg(".my_log.data")
#pragma const_seg(".my_log.text.const")
#pragma code_seg(".my_log.text")
#endif

#include "my_common.h"

char LOG_BUFFER[UART_LOG_MAX_LEN];
static OS_MUTEX my_log_mutex;

/* 增加日志锁保护机制：
 * 防止关掉底层SDK日志后(宏控TCFG_DEBUG_UART_ENABLE)，系统起来打印日志异常导致系统奔溃
 */
void my_log_mutex_init(void)
{
    int ret = os_mutex_create(&my_log_mutex);
    printf("%s=>ret:%d", __func__, ret);
}

void my_log_printf_internal(int log_level, char *fmt, ...)
{
    va_list ap;

    os_mutex_pend(&my_log_mutex, 0);

    memset(LOG_BUFFER, 0, sizeof(LOG_BUFFER));

    va_start(ap, fmt);
    vsnprintf(LOG_BUFFER, sizeof(LOG_BUFFER) - 2, fmt, ap);
    va_end(ap);

    strcat(LOG_BUFFER, "\r\n");

#if CONFIG_DEBUG_ENABLE
    printf("%s", LOG_BUFFER);
#else
    my_uart_write_data(MY_SHELL_PORT, (uint8 *)LOG_BUFFER, strlen(LOG_BUFFER));
#endif

    os_mutex_post(&my_log_mutex);
}
