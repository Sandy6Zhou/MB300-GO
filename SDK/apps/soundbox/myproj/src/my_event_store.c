/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : my_event_store.c
 * Brief      : EMS 事件存储：DC 仪态开关节次与告警 FIFO，RAM/VM 持久化
 * Version    : V1.0.0
 * Author     : songshiqing(songshiqing@jimiiot.com)
 * Date       : 2026-04-02
 ************************************************************************************************/

#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".my_evt_store.data.bss")
#pragma data_seg(".my_evt_store.data")
#pragma const_seg(".my_evt_store.text.const")
#pragma code_seg(".my_evt_store.text")
#endif

#include "my_common.h"

/* 运行态关键日志开关：跨日、开关边沿、告警上升沿。 */
#define MY_EVT_LOG_EVENT_EN 1

/* 日统计 VM：结构体首字段 uint32 magic，与设备小端一致（片上 Flash 中低字节在前） */
#define MY_DEP_EVT_BLOB_MAGIC 0x454D5344u /* EMSD */
#define MY_DEP_EVT_BLOB_VER   2u          /* 布局版本号 */

/* 告警 VM：结构体首字段 uint32 magic，与设备小端一致（片上 Flash 中低字节在前） */
#define MY_DEP_ALARM_BLOB_MAGIC 0x454D5341u /* EMSA */
#define MY_DEP_ALARM_BLOB_VER   1u          /* 布局版本号 */

/* DC 0x0001：仅低 5 位参与开关节次；0x0003：低 11 位参与告警上升沿 */
#define MY_EVT_SW_MASK    0x001Fu
#define MY_EVT_FAULT_MASK 0x07FFu

#pragma pack(push, 1)
/*
 * 日统计状态
 */
typedef struct
{
    uint32 magic;                                   // MAGIC NUM
    uint8 ver;                                      // 版本
    my_ems_day_wire_t records[MY_EVT_DAY_SLOT_MAX]; // 日统计记录数组
} my_evt_day_state_t;

/*
 * 告警状态
 */
typedef struct
{
    uint32 magic;                               // MAGIC NUM
    uint8 ver;                                  // 版本
    uint16 head;                                // 头索引
    uint16 tail;                                // 尾索引
    uint16 count;                               // 有效数量
    uint16 seq_next;                            // 下一个序号
    my_ems_alarm_wire_t ring[MY_EVT_ALARM_CAP]; // 告警环形队列
} my_evt_alarm_state_t;
#pragma pack(pop)

static my_evt_day_state_t s_day_evt;
static my_evt_alarm_state_t s_alarm_evt;

/* 置 1 表示下次采样前重置函数内 static 状态（兼容重复 init 场景）。 */
static uint8 s_sample_state_need_reinit = 1;

/* ========== 时间处理：当日零点 UTC 秒 ========== */

/** RTC 是否参与当日零点时间戳/unix；否则返回 0 */
static uint8 evt_time_ok(const struct sys_time *t)
{
    /* 空指针直接视为时间不可用。 */
    if (t == NULL)
    {
        return 0;
    }

    // TODO 正式版本改为 2026，RTC 默认为 2020
    if (t->year < 2000 || t->year > 2099)
    {
        return 0;
    }
    return 1;
}

/** 将当前时间折算为“当日零点 UTC 秒”；失败返回 0 */
static uint32 evt_day_utc0(void)
{
    struct sys_time t;
    struct sys_time t0;

    /* 先读当前时间；失败时返回 0，表示“当前时间不可用”。 */
    my_get_sys_time(&t);
    if (!evt_time_ok(&t))
    {
        return 0;
    }

    /* 将时分秒归零，得到“当天零点”的 unix 秒。 */
    t0 = t;
    t0.hour = 0;
    t0.min = 0;
    t0.sec = 0;
    return my_mytime_2_utc_sec(&t0);
}

/** 获取当前告警时间戳：RTC 无效时返回 0 */
static uint32 evt_now_unix_safe(void)
{
    struct sys_time t;

    /* 与日统计同口径：时间不可用时返回 0。 */
    my_get_sys_time(&t);
    if (!evt_time_ok(&t))
    {
        return 0;
    }
    return my_mytime_2_utc_sec(&t);
}

/* ========== 日统计记录（按自然日分组；未校时不创建记录） ========== */

/** 重置一条日统计记录（day record 内存态）为默认值 */
static void evt_day_record_reset(my_ems_day_wire_t *record)
{
    if (record == NULL)
    {
        return;
    }
    memset(record, 0, sizeof(*record));
    record->payload_ver = 0;
}

/** 按 day_unix 查找日统计记录索引；未找到返回 -1 */
static int evt_find_day_record(uint32 day_unix)
{
    uint8 idx = 0;

    /* 未校时：逻辑上直接忽略，不创建记录。 */
    if (day_unix == 0)
    {
        return -1;
    }

    /* 顺序查找同一天记录，命中即返回索引。 */
    for (idx = 0; idx < MY_EVT_DAY_SLOT_MAX; idx++)
    {
        if (s_day_evt.records[idx].day_unix_utc0 == day_unix)
        {
            return idx;
        }
    }
    return -1;
}

/** 记录数组 MY_EVT_DAY_SLOT_MAX 占满时，淘汰 day_unix 最旧的一条 */
static int evt_alloc_day_record(void)
{
    int i = 0;
    int idx = -1;
    uint32 local_oldest_day_unix = 0xFFFFFFFFu; // 当前最旧自然日时间戳

    /* 遍历各记录项，找到第一个未使用的索引 */
    for (i = 0; i < MY_EVT_DAY_SLOT_MAX; i++)
    {
        if (s_day_evt.records[i].day_unix_utc0 == 0)
        {
            return i;
        }
    }

    /* 遍历各记录项，找到 day_unix 最小的索引（淘汰最旧自然日） */
    for (i = 0; i < MY_EVT_DAY_SLOT_MAX; i++)
    {
        if (s_day_evt.records[i].day_unix_utc0 < local_oldest_day_unix)
        {
            local_oldest_day_unix = s_day_evt.records[i].day_unix_utc0;
            idx = i;
        }
    }
    return idx;
}

/**
 * 按 day_unix 获取或创建日记录。
 * - day_unix == 0：表示未校时，直接返回 NULL，不创建记录。
 * - day_unix != 0：命中则返回现有记录；未命中则新建后返回。
 */
static my_ems_day_wire_t *evt_get_or_create_day_record(uint32 day_unix)
{
    int idx = evt_find_day_record(day_unix);

    /* 未校时直接返回 NULL。 */
    if (day_unix == 0)
    {
        return NULL;
    }

    /* 已存在同一天记录，直接复用。 */
    if (idx >= 0)
    {
        return &s_day_evt.records[idx];
    }

    /* 不存在则分配新记录：优先空位，否则淘汰最旧。 */
    idx = evt_alloc_day_record();
    if (idx < 0)
    {
        return NULL;
    }

    /* 重置新记录为默认值。 */
    evt_day_record_reset(&s_day_evt.records[idx]);
    s_day_evt.records[idx].day_unix_utc0 = day_unix;
    s_day_evt.records[idx].payload_ver = 0;
    return &s_day_evt.records[idx];
}

/** 当日零点时间戳变化时，确保对应日记录存在；若本次新建返回 1。 */
static uint8 evt_on_day_change(uint32 new_day_ts)
{
    int idx = evt_find_day_record(new_day_ts);

    /* 确保对应日统计记录存在；若本次新建记录则返回 1。 */
    my_ems_day_wire_t *rec = evt_get_or_create_day_record(new_day_ts);

    /* 取不到记录（例如未校时）时返回 0。 */
    if (rec == NULL)
    {
        return 0;
    }

    /* 记录存在，返回 1。 */
    return (idx < 0) ? 1 : 0;
}

/** 获取“当前当日零点时间戳”对应的日记录指针；失败返回 NULL。 */
static my_ems_day_wire_t *evt_get_today_record(void)
{
    uint32 day_ts = evt_day_utc0();
    my_ems_day_wire_t *rec = evt_get_or_create_day_record(day_ts);

    return rec;
}

/* ========== 告警 FIFO（故障寄存器上升沿 → kind + ts，满则丢最旧） ========== */

static void evt_alarm_push(uint8 kind, uint32 ts_unix)
{
    my_ems_alarm_wire_t alarm;

    /* 队列满时先丢最旧一条，保证新告警一定能入队。 */
    if (s_alarm_evt.count >= MY_EVT_ALARM_CAP)
    {
        /* 丢最旧 */
        s_alarm_evt.head = (uint16)((s_alarm_evt.head + 1) % MY_EVT_ALARM_CAP);
        s_alarm_evt.count--;
    }

    /* seq 为单调序号；0 作为保留值，回绕后跳到 1。 */
    alarm.seq = s_alarm_evt.seq_next++;
    if (s_alarm_evt.seq_next == 0)
    {
        s_alarm_evt.seq_next = 1;
    }
    alarm.kind = kind;
    alarm.ts_unix = ts_unix;

    /* 新记录写到队尾，再推进队尾索引和有效数量。 */
    s_alarm_evt.ring[s_alarm_evt.tail] = alarm;
    s_alarm_evt.tail = (uint16)((s_alarm_evt.tail + 1) % MY_EVT_ALARM_CAP);
    s_alarm_evt.count++;
}

/* ========== VM：日数据块/告警数据块均为定长结构体 ========== */

static void evt_save_day_vm(void)
{
    int ret = 0;

    /* 写前仅刷新头字段。 */
    s_day_evt.magic = MY_DEP_EVT_BLOB_MAGIC;
    s_day_evt.ver = MY_DEP_EVT_BLOB_VER;

    ret = syscfg_write(CFG_EMS_DAY_STATS_BLOB, &s_day_evt, sizeof(s_day_evt));
    if (ret <= 0)
    {
        my_log_printf(1, "EMS vm day save fail ret=%d", ret);
    }
}

/** 长度或 MAGIC NUM/版本不对则不改 RAM 日统计状态 */
static void evt_load_day_vm(void)
{
    my_evt_day_state_t pack;
    int ret = 0;

    /* 长度必须完全匹配，避免半包数据误解析。 */
    ret = syscfg_read(CFG_EMS_DAY_STATS_BLOB, &pack, sizeof(pack));
    if (ret != (int)sizeof(pack))
    {
        return;
    }

    /* MAGIC NUM / 版本校验通过后再恢复运行态。 */
    if (pack.magic != MY_DEP_EVT_BLOB_MAGIC || pack.ver != MY_DEP_EVT_BLOB_VER)
    {
        return;
    }

    memcpy(s_day_evt.records, pack.records, sizeof(s_day_evt.records));
}

static void evt_save_alarm_vm(void)
{
    int ret = 0;

    /* 写前仅刷新头字段。 */
    s_alarm_evt.magic = MY_DEP_ALARM_BLOB_MAGIC;
    s_alarm_evt.ver = MY_DEP_ALARM_BLOB_VER;

    ret = syscfg_write(CFG_EMS_ALARM_FIFO_BLOB, &s_alarm_evt, sizeof(s_alarm_evt));
    if (ret <= 0)
    {
        my_log_printf(1, "EMS vm alarm save fail ret=%d", ret);
    }
}

/** 长度或 MAGIC NUM/版本不对则不改 RAM 告警状态 */
static void evt_load_alarm_vm(void)
{
    my_evt_alarm_state_t pack;
    int ret = 0;

    /* 长度必须完全匹配，避免半包数据误解析。 */
    ret = syscfg_read(CFG_EMS_ALARM_FIFO_BLOB, &pack, sizeof(pack));
    if (ret != (int)sizeof(pack))
    {
        return;
    }

    /* MAGIC NUM 或 版本校验通过后再恢复运行态。 */
    if (pack.magic != MY_DEP_ALARM_BLOB_MAGIC || pack.ver != MY_DEP_ALARM_BLOB_VER)
    {
        return;
    }

    /* 对计数和序号做边界修正，防止脏数据影响运行。 */
    s_alarm_evt.head = pack.head % MY_EVT_ALARM_CAP;
    s_alarm_evt.tail = pack.tail % MY_EVT_ALARM_CAP;

    /* 防止脏数据影响运行。如果超出容量，则截断。 */
    s_alarm_evt.count = pack.count;
    if (s_alarm_evt.count > MY_EVT_ALARM_CAP)
    {
        s_alarm_evt.count = MY_EVT_ALARM_CAP;
    }

    s_alarm_evt.seq_next = pack.seq_next;
    if (s_alarm_evt.seq_next == 0)
    {
        s_alarm_evt.seq_next = 1;
    }

    memcpy(s_alarm_evt.ring, pack.ring, sizeof(s_alarm_evt.ring));
}

/* 通用头校验：读取 cfg 的前 4 字节，按小端拼成 uint32，与期望 MAGIC NUM 比较。 */
static uint8 evt_check_vm_magic(int cfg_id, uint32 expect_magic)
{
    uint8 raw[8];
    int ret = syscfg_read(cfg_id, raw, sizeof(raw));
    uint32 magic = 0;

    /* 仅做快速头校验，不解析完整内容。 */
    if (ret < (int)sizeof(uint32))
    {
        return 0;
    }

    magic = (uint32)raw[0] | ((uint32)raw[1] << 8) | ((uint32)raw[2] << 16) | ((uint32)raw[3] << 24);
    return (magic == expect_magic) ? 1 : 0;
}

/* ========== 对外 API ========== */

/** 上电：先清 RAM 运行时态，再从 VM 恢复日统计记录与告警环形队列（FIFO） */
void my_evt_store_init(void)
{
    /* 先清运行态，再从 VM 恢复。 */
    memset(&s_day_evt, 0, sizeof(s_day_evt));
    memset(&s_alarm_evt, 0, sizeof(s_alarm_evt));
    s_alarm_evt.seq_next = 1;
    s_sample_state_need_reinit = 1;

    evt_load_day_vm();
    evt_load_alarm_vm();

    /* 启动必须可见 */
    printf("[EMS] init ok");
}

/** DC 采样入口：维护跨周期状态（函数内 static）并更新日统计与告警。 */
void my_evt_on_dc_sample(uint16 reg_sw, uint16 reg_fault)
{
    static uint32 s_day_ts_cached = 0xFFFFFFFFu; // 缓存当前当日零点时间戳
    static uint16 s_prev_switch_bits = 0xFFFFu;  // 缓存上一周期的开关位图
    static uint16 s_prev_alarm_bits = 0;         // 缓存上一周期的告警位图
    uint32 day_ts = evt_day_utc0();              // 当前当日零点时间戳
    uint8 day_created = 0;                       // 是否新建当日记录

    /* 重置跨周期状态。 */
    if (s_sample_state_need_reinit)
    {
        s_day_ts_cached = 0xFFFFFFFFu;
        s_prev_switch_bits = 0xFFFFu;
        s_prev_alarm_bits = 0;
        s_sample_state_need_reinit = 0;
    }

    /* 当日零点时间戳变化：表示跨天或 RTC 状态变化，需要确保目标日记录存在。 */
    if (day_ts != s_day_ts_cached)
    {
#if MY_EVT_LOG_EVENT_EN
        my_log_printf(1, "[EMS] day_ts -> 0x%08lx", (unsigned long)day_ts);
#endif
        s_day_ts_cached = day_ts;
        day_created = evt_on_day_change(day_ts);

        /* 仅在“新建当日记录且已校时”时做一次保存。 */
        if (day_created && day_ts != 0)
        {
            evt_save_day_vm();
            evt_save_alarm_vm();
        }
    }

    /* 获取“当前当日零点时间戳对应”的日统计打包数据指针；未校时时返回 NULL。 */
    my_ems_day_wire_t *today_wire = evt_get_today_record();
    if (today_wire == NULL)
    {
        return;
    }

    /* 开关节次：0x0001 低 5 位，位 0..4 对应 电源/逆变/USB/DC5521/LED。 */
    {
        uint16 sw = (uint16)(reg_sw & MY_EVT_SW_MASK);
        uint16 prev = (uint16)(s_prev_switch_bits == 0xFFFFu ? sw : (s_prev_switch_bits & MY_EVT_SW_MASK));
        uint16 edge = (uint16)(sw ^ prev);
        uint8 bit_pos; // 位位置

        /* 首次采样只建立基线，不计边沿。 */
        if (s_prev_switch_bits != 0xFFFFu && edge)
        {
            for (bit_pos = 0; bit_pos < 5; bit_pos++)
            {
                if (edge & (1u << bit_pos))
                {
                    switch (bit_pos)
                    {
                        case 0: // 电源
                            today_wire->cnt_power++;
                            break;
                        case 1: // 逆变
                            today_wire->cnt_inv++;
                            break;
                        case 2: // USB
                            today_wire->cnt_usb++;
                            break;
                        case 3: // DC5521
                            today_wire->cnt_dc5521++;
                            break;
                        case 4: // LED
                            today_wire->cnt_led++;
                            break;
                        default:
                            break;
                    }
#if MY_EVT_LOG_EVENT_EN
                    my_log_printf(1, "[EMS] sw edge bit%u pwr=%u inv=%u usb=%u dc=%u led=%u", (unsigned)bit_pos,
                                  (unsigned)today_wire->cnt_power, (unsigned)today_wire->cnt_inv,
                                  (unsigned)today_wire->cnt_usb, (unsigned)today_wire->cnt_dc5521,
                                  (unsigned)today_wire->cnt_led);
#endif
                }
            }
        }
        /* 更新上一周期快照，供下一次边沿比较。 */
        s_prev_switch_bits = sw;
    }

    /* 告警：0x0003 低 11 位上升沿入 FIFO。 */
    {
        uint16 cur = (uint16)(reg_fault & MY_EVT_FAULT_MASK);
        uint16 prev = (uint16)(s_prev_alarm_bits & MY_EVT_FAULT_MASK);
        uint16 rising = (uint16)(cur & (uint16)(~prev));
        uint8 bit_pos;
        uint32 ts = evt_now_unix_safe();

        if (rising)
        {
            for (bit_pos = 0; bit_pos <= 10; bit_pos++)
            {
                if (rising & (1u << bit_pos))
                {
                    /* 0..10 直接透传为告警 kind。 */
                    evt_alarm_push(bit_pos, ts);
#if MY_EVT_LOG_EVENT_EN
                    my_log_printf(1, "[EMS] alarm rising bit%u -> kind=%u ts=%lu seq pending~%u",
                                  (unsigned)bit_pos, (unsigned)bit_pos, (unsigned long)ts,
                                  (unsigned)s_alarm_evt.count);
#endif
                }
            }
        }
        s_prev_alarm_bits = cur;
    }
}

/** 立即写入两块 EMS 数据块 */
void my_evt_flush_vm(void)
{
    evt_save_day_vm();
    evt_save_alarm_vm();
}

/** 告警环形队列（FIFO）当前有效条数：与 head/tail/count 一致；容量满时策略为丢最旧 */
int my_evt_alarm_pending_count(void)
{
    return s_alarm_evt.count;
}

uint8 my_evt_day_vm_magic_ok(void)
{
    return evt_check_vm_magic(CFG_EMS_DAY_STATS_BLOB, MY_DEP_EVT_BLOB_MAGIC);
}

uint8 my_evt_alarm_vm_magic_ok(void)
{
    return evt_check_vm_magic(CFG_EMS_ALARM_FIFO_BLOB, MY_DEP_ALARM_BLOB_MAGIC);
}

