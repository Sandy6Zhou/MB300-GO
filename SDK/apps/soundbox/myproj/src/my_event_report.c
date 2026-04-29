/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : my_event_report.c
 * Brief      : EMS 主动上报流程模块
 * Version    : V1.0.0
 * Author     : songshiqin (songshiqin@jimiiot.com)
 * Date       : 2026-04-15
 ************************************************************************************************/
#include "my_common.h"

typedef enum
{
    MY_EVENT_REPORT_STAGE_DAY = 0, // day 阶段
    MY_EVENT_REPORT_STAGE_ALARM,   // alarm 阶段
    MY_EVENT_REPORT_STAGE_DONE,    // 上报结束
} my_event_report_stage_t;

#define BLE_CONNECT_REPORT_DELAY_MS 500        // 连接后延时启动
static uint8 s_ble_connect_report_started = 0; // 本轮连接上报已启动
static my_event_report_stage_t s_event_report_stage = MY_EVENT_REPORT_STAGE_DONE;

static void try_send_next_connect_report(void);
static void ble_connect_report_delay_cb(void *priv);

/* 推进连接后的历史上报。 */
static void try_send_next_connect_report(void)
{
    uint8 buf[BLE_SVC_RX_MAX_LEN] = {0};
    uint16 len = 0;
    int ret = 0;

    /* 仅在连接有效且会话已启动时推进。 */
    if (!s_ble_connect_report_started || check_connect_id_enable() == 0)
    {
        return;
    }

    /* DAY 阶段发送 FF01。 */
    if (s_event_report_stage == MY_EVENT_REPORT_STAGE_DAY)
    {
        ret = my_evt_prepare_next_day_expansion_packet(buf, sizeof(buf), &len);
        if (ret > 0 && len > 0 && len <= (BLE_SERVER_MAX_DATA_LEN - 4))
        {
            /* 发一包 day，等待 ACK。 */
            my_log_printf(1, "[EMS] send next day report len=%u", (unsigned)len);
            ble_comu_response_or_expansion_cmd(BLE_DATA_TYPE_EXPANSION_MODULE, buf, (uint8)len);
            return;
        }
        /* DAY 阶段结束后切到 alarm。 */
        s_event_report_stage = MY_EVENT_REPORT_STAGE_ALARM;
        my_log_printf(1, "[EMS] day stage done -> alarm");
    }

    /* ALARM 阶段发送 5C01。 */
    if (s_event_report_stage == MY_EVENT_REPORT_STAGE_ALARM)
    {
        len = 0;
        ret = my_evt_prepare_next_alarm_packet(buf, sizeof(buf), &len);
        if (ret > 0 && len > 0 && len <= (BLE_SERVER_MAX_DATA_LEN - 4))
        {
            /* 发一包 alarm，等待 ACK。 */
            my_log_printf(1, "[EMS] send next alarm report len=%u", (unsigned)len);
            ble_comu_response_or_expansion_cmd(BLE_DATA_TYPE_STD_ALARM_REPORT, buf, (uint8)len);
            return;
        }
        /* ALARM 阶段结束后收尾。 */
        s_event_report_stage = MY_EVENT_REPORT_STAGE_DONE;
        my_log_printf(1, "[EMS] connect reports done");
        my_evt_tx_session_stop();
        s_ble_connect_report_started = 0;
    }
}

static void ble_connect_report_delay_cb(void *priv)
{
    (void)priv;
    /* 延时到期后切到 BLE 线程内启动上报，避免定时器上下文直接推进状态机。 */
    my_send_msg(MOD_MAIN, MOD_BLE, MY_MSG_BLE_REPORT_START);
}

void my_event_report_schedule(void)
{
    /* 启动延时定时器。 */
    my_start_timer(MY_TIMER_BLE_CONNECT_REPORT, BLE_CONNECT_REPORT_DELAY_MS, false, ble_connect_report_delay_cb);
    my_log_printf(1, "[EMS] connect reports scheduled");
}

void my_event_report_send_handle(void)
{
    /* 断开后不再启动。 */
    if (check_connect_id_enable() == 0)
    {
        my_log_printf(1, "[EMS] connect reports skip: ble disconnected");
        return;
    }

    /* 同一轮连接只启动一次。 */
    if (s_ble_connect_report_started)
    {
        my_log_printf(1, "[EMS] connect reports already running");
        return;
    }

    /* 新会话从 day 阶段开始。 */
    s_ble_connect_report_started = 1;
    s_event_report_stage = MY_EVENT_REPORT_STAGE_DAY;
    my_evt_tx_session_start();
    try_send_next_connect_report();
}

void my_event_report_on_disconnect(void)
{
    /* 断开时先停延时定时器。 */
    my_stop_timer(MY_TIMER_BLE_CONNECT_REPORT);

    if (s_ble_connect_report_started)
    {
        /* 断开时统一收尾。 */
        my_log_printf(1, "[EMS] connect reports disconnect stop");
        my_evt_tx_session_stop();
    }
    s_ble_connect_report_started = 0;
    s_event_report_stage = MY_EVENT_REPORT_STAGE_DONE;
}

void my_event_report_on_expansion_ack(uint8 is_ok)
{
    /* 仅处理 day 阶段的 FF01 ACK。 */
    if (!s_ble_connect_report_started || s_event_report_stage != MY_EVENT_REPORT_STAGE_DAY)
    {
        return;
    }

    /* DAY 阶段可继续时直接推进。 */
    if (my_evt_on_day_expansion_ack(is_ok))
    {
        try_send_next_connect_report();
        return;
    }

    if (is_ok)
    {
        /* DAY 阶段结束后切到 alarm。 */
        s_event_report_stage = MY_EVENT_REPORT_STAGE_ALARM;
        try_send_next_connect_report();
    }
    else
    {
        /* ACK 失败则结束本轮会话。 */
        s_event_report_stage = MY_EVENT_REPORT_STAGE_DONE;
        my_evt_tx_session_stop();
        s_ble_connect_report_started = 0;
    }
}

void my_event_report_on_std_alarm_ack(uint8 is_ok)
{
    /* 仅处理 alarm 阶段的 5C01 ACK。 */
    if (!s_ble_connect_report_started || s_event_report_stage != MY_EVENT_REPORT_STAGE_ALARM)
    {
        return;
    }

    /* ALARM 阶段可继续时直接推进。 */
    if (my_evt_on_alarm_report_ack(is_ok))
    {
        try_send_next_connect_report();
        return;
    }

    /* ALARM 阶段结束后统一收尾。 */
    s_event_report_stage = MY_EVENT_REPORT_STAGE_DONE;
    my_evt_tx_session_stop();
    s_ble_connect_report_started = 0;
}
