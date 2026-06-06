#ifndef MY_COMMON_H
#define MY_COMMON_H

#include "system/includes.h"
#include "spi.h"
#include "gpio.h"
#include "uart.h"
#include "debug.h"
#include "clock.h"
#include "sdk_config.h"
#include "app_config.h"
#include "audio_config.h"
#include "my_platform_time.h"
#include "app_version.h"
#include "app_tone.h"
#include "asm/efuse.h"
#include "boot.h"

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
    MOD_BLE,
    MOD_DC_UART,
    MAX_MY_MOD_TYPE,
}module_type;

typedef enum
{
    MY_TIMER_TEST = 0,
    MY_TIMER_DC_POLL,            /* DC轮询定时器：100ms，init后常驻运行 */
    MY_TIMER_DC_CTRL_ACK,        /* DC控制ACK定时器：开关控制后每500ms检查一次结果，最多3次 */
    MY_TIMER_BLE_CONNECT_REPORT, /* 上报延迟定时器：蓝牙本地存储数据上报延时500ms */
    MY_TIMER_MAX_ID,
} MY_E_TIMER;

typedef enum
{
    MY_MSG_BASE_MSG = 0,
    MY_MSG_TEST,
    MY_MSG_UART_RECV,
    MY_MSG_BLE_RX,
    MY_MSG_BLE_REPORT_START, /* EMS上报开始：由延时定时器投递到BLE线程 */
    MY_MSG_BLE_REPORT_STOP,  /* EMS上报结束：由断开流程投递到BLE线程 */
    MY_MSG_DC_POLL_TICK,   /* DC 100ms 定时消息：dc_poll_timer_cb 发送 */
    MY_MSG_DC_POLL_START,  /* BLE连接：置0x0001 Bit5（蓝牙图标） */
    MY_MSG_DC_POLL_STOP,   /* BLE断开：清0x0001 Bit5（蓝牙图标） */
    MY_MSG_DC_CTRL_REQ,    /* DC控制请求：BLE 收到 MB300_SW_xx 后发往 DC 任务 */
} MY_MAIN_TASK_MSG;

#include "my_uart.h"
#include "my_shell.h"
#include "my_tool.h"
#include "my_log.h"
#include "my_vm_param.h"
#include "my_event_store.h"
#include "my_led_spi.h"
#include "my_findmy_protocol.h"
#include "ble_comu_def.h"
#include "ble_comunication.h"
#include "my_ble.h"
#include "my_dc_uart.h"
#include "my_gpio.h"
#include "my_event_report.h"
#include "my_cmd_handler.h"

extern char *g_my_task_info[MAX_MY_MOD_TYPE];

#endif