/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : my_event_store.h
 * Brief      : EMS 事件存储模块头文件（开关节次、告警 FIFO、VM 数据块）
 * Version    : V1.0.0
 * Author     : songshiqing(songshiqing@jimiiot.com)
 * Date       : 2026-04-02
 ************************************************************************************************/

#ifndef MY_EVENT_STORE_H
#define MY_EVENT_STORE_H

#define MY_EVT_DAY_SLOT_MAX 7  // 7 天
#define MY_EVT_ALARM_CAP    20 // 20 条告警

#pragma pack(push, 1)
/**
 * 日统计记录（day record）对外数据结构（VM 与后续蓝牙 0x800D 若对齐可复用布局；当前仅累计 5 路开关，chg/dsg 恒 0）。
 */
typedef struct
{
    uint8 payload_ver;
    uint32 day_unix_utc0;
    uint16 cnt_power;
    uint16 cnt_inv;
    uint16 cnt_usb;
    uint16 cnt_dc5521;
    uint16 cnt_led;
    uint16 chg_min_total; /* 预留，当前保持 0 */
    uint16 dsg_min_total; /* 预留，当前保持 0 */
} my_ems_day_wire_t;

/** 单条告警 7B：seq 单调；kind 为故障位 bit(0..10) 直传；ts_unix 无 RTC 时为 0 */
typedef struct
{
    uint16 seq;
    uint8 kind;
    uint32 ts_unix;
} my_ems_alarm_wire_t;

#pragma pack(pop)

/** 上电：从 VM 恢复日统计记录（day record）与告警环形队列（FIFO） */
void my_evt_store_init(void);

/** DC 寄存器就绪后调用：reg_sw=0x0001 低 5 位边沿计次，reg_fault=0x0003 低 11 位上升沿写入告警环形队列（FIFO） */
void my_evt_on_dc_sample(uint16 reg_sw, uint16 reg_fault);

/** 立即持久化两块 EMS 数据块 */
void my_evt_flush_vm(void);

/** 当前告警 FIFO 条数 */
int my_evt_alarm_pending_count(void);

/** VM 日数据块 MAGIC NUM 头校验（通过=1，失败=0） */
uint8 my_evt_day_vm_magic_ok(void);
/** VM 告警数据块 MAGIC NUM 头校验（通过=1，失败=0） */
uint8 my_evt_alarm_vm_magic_ok(void);

#endif /* MY_EVENT_STORE_H */
