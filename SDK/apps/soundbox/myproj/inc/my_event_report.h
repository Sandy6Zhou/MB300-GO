/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : my_event_report.h
 * Brief      : EMS 主动上报流程模块头文件
 * Version    : V1.0.0
 * Author     : songshiqin (songshiqin@jimiiot.com)
 * Date       : 2026-04-15
 ************************************************************************************************/
#ifndef MY_EVENT_REPORT_H
#define MY_EVENT_REPORT_H

/** 启动一轮主动上报会话 */
void my_event_report_send_handle(void);
/** 连接后延时调度主动上报 */
void my_event_report_schedule(void);
/** 断开时停止上报并统一收尾 */
void my_event_report_on_disconnect(void);
/** 处理 FF01 ACK 并推进 day 阶段 */
void my_event_report_on_expansion_ack(uint8 is_ok);
/** 处理 5C01 ACK 并推进 alarm 阶段 */
void my_event_report_on_std_alarm_ack(uint8 is_ok);

#endif /* MY_EVENT_REPORT_H */
