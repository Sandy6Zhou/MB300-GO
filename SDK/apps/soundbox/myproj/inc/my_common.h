#ifndef MY_COMMON_H
#define MY_COMMON_H

#include "system/includes.h"
#include "uart.h"
#include "debug.h"
#include "clock.h"
#include "sdk_config.h"
#include "app_config.h"

typedef unsigned int    my_task_handle;
typedef unsigned int    my_queue_t;

typedef unsigned long long  uint64;

typedef unsigned int    uint32;
typedef signed int      int32;

typedef unsigned short  uint16;
typedef signed short    int16;

typedef unsigned char   uint8;
typedef signed char     int8;

#define JM_Sleep    msleep

typedef struct {
    uint16 id;
    bool isPeriod;
}
my_timer_t;

typedef enum
{
    MOD_MAIN,             //主处理程序
    MOD_SHELL,
    MAX_MY_MOD_TYPE,
}module_type;

typedef enum
{
    MY_TIMER_TEST = 0,
    MY_TIMER_MAX_ID,
} MY_E_TIMER;

typedef enum
{
    MY_MSG_BASE_MSG = 0,
    MY_MSG_TEST,
    MY_MSG_UART_RECV,
}MY_MAIN_TASK_MSG;

#include "my_uart.h"
#include "my_shell.h"
#include "my_tool.h"
#include "my_log.h"
#include "my_vm_param.h"

extern char *g_my_task_info[MAX_MY_MOD_TYPE];

#endif