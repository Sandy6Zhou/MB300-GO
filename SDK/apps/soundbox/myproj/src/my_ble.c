/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : my_ble.c
 * Brief      : 设备蓝牙消息接收模块
 * Version    : V1.0.0
 * Author     : 周森达(zhousenda@jimiiot.com)
 * Date       : 2026-01-14
************************************************************************************************/
#include "my_common.h"

void my_ble_task(void *p_arg)
{
    int ret = 0;
    int msg[8] = {0};

    my_log_printf(1, "############# my_ble_task is runing ##################");

    while (1)
    {
        ret = os_taskq_pend(NULL, msg, ARRAY_SIZE(msg));
        if (ret != OS_TASKQ) {
            continue;
        }

        switch (msg[0])
        {
            case MY_MSG_BLE_RX:
            {
                ble_rx_proc_handle();
                break;
            }

            case MY_MSG_BLE_CAN_SEND_NOW:
            {
                ble_server_on_can_send_now();
                break;
            }

            case MY_MSG_BLE_REPORT_START:
            {
                my_event_report_send_handle();
                break;
            }

            case MY_MSG_BLE_REPORT_STOP:
            {
                my_event_report_on_disconnect();
                break;
            }

            default:
                break;
        }
    }

}
