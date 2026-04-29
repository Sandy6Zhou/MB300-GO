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

/* 打印 FF01/5C01 明文（仅日志，不发 GATT）。*/
#define MY_EMS_BLE_PROTO_LOG_EN 1

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
static uint8 s_day_vm_need_commit = 0;   /* day RAM 有删除改动且尚未提交 VM */
static uint8 s_alarm_vm_need_commit = 0; /* 告警 FIFO RAM 有删除改动且尚未提交 VM */

static void evt_save_day_vm(void);
static void evt_save_alarm_vm(void);

/*
 * 连接上报会话上下文：
 * - day_cursor/day_last_sent：day 记录项扫描指针与最近一次发出的记录项
 * - day_wait_ack/alarm_wait_ack：day/alarm 等待 ACK
 * - alarm_last_sent：当前这包 5C01 中实际带了多少条告警，ACK 成功后按这个窗口删除
 */
typedef struct
{
    uint8 started;  /* 是否已开始会话 */

    uint8 day_cursor;   /* day 记录项扫描指针 */
    uint8 day_last_sent; /* 最近一次发出的记录项 */
    uint8 day_wait_ack;  /* day 是否等待 ACK */
    uint8 day_stopped;   /* day 是否已停止会话 */

    uint16 alarm_last_sent; /* 当前这包 5C01 中实际带了多少条告警，ACK 成功后按这个窗口删除 */
    uint8 alarm_wait_ack;   /* alarm 等待 ACK */
} my_evt_tx_ctx_t;

static my_evt_tx_ctx_t s_evt_tx = {0};

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

/** 告警环形队列：返回最多 max_count 条告警记录，不改变队列状态 */
static int evt_alarm_peek_batch(my_ems_alarm_wire_t *out, uint16 max_count)
{
    uint16 i = 0;
    uint16 actual_count = 0;
    uint16 idx = 0;

    if (out == NULL || max_count == 0)
    {
        return 0;
    }

    /* 获取当前 FIFO 有效数量。 */
    actual_count = s_alarm_evt.count;
    /* 如果有效数量大于最大数量，则截断。 */
    if (actual_count > max_count)
    {
        actual_count = max_count;
    }

    /* 只拷贝，不删除；真正删除发生在 ACK 成功之后。 */
    idx = s_alarm_evt.head;
    for (i = 0; i < actual_count; i++)
    {
        out[i] = s_alarm_evt.ring[idx];
        idx = (uint16)((idx + 1) % MY_EVT_ALARM_CAP);
    }
    return actual_count;
}

// FF01/5C01 明文组包辅助
#define EVT_BLE_EMS_IOS_SAFE_PKT 180 // iOS 安全包长度
#define EVT_STD_ALARM_PL_MAX     (EVT_BLE_EMS_IOS_SAFE_PKT - 5)
#define EVT_STD_ALARM_HDR_SZ     3 // 3 字节帧头：0x8A, len, 0xB3
#define EVT_STD_ALARM_ENTRY_SZ   6 // 6 字节告警条目：4B UTC 时间戳，2B 告警位号

static uint16 evt_ble_w16be(uint8 *dst, uint16 val)
{
    if (dst == NULL)
    {
        my_log_printf(1, "[EMS] evt_ble_w16be dst null");
        return 0;
    }

    uint16 be = my_swap16(val); // wire 大端；RAM 小端时 swap 后再按字节拷贝

    memcpy(dst, &be, sizeof(be));
    return 2;
}

static uint16 evt_ble_w32be(uint8 *dst, uint32 val)
{
    if (dst == NULL)
    {
        my_log_printf(1, "[EMS] evt_ble_w32be dst null");
        return 0;
    }

    uint32 be = my_swap32(val); // 同上，32bit

    memcpy(dst, &be, sizeof(be));
    return 4;
}

static uint16 evt_ble_plain_round_up_16(uint16 len)
{
    uint16 send_len = (uint16)((len / 16u) * 16u); // 已整除 16 时保持不动

    if (len % 16u)
    {
        send_len = (uint16)(send_len + 16u); // 否则进一档到下一个 16 边界，padding 填 0
    }
    return send_len;
}

/* FF01 TLV 里 module_id / len 的 u16 变长编码；成功返回字节数，失败 0 */
static uint8 evt_ble_encode_varint_u16(uint16 value, uint8 *out, uint8 out_size)
{
    uint8 idx = 0;
    uint8 b = 0;

    /* 参数合法性检查 */
    if (out == NULL || out_size == 0)
    {
        return 0;
    }

    /* 编码 value 为 varint 格式 */
    do
    {
        b = (uint8)(value & 0x7Fu); // 本字节承载低 7 位

        /* 右移 7 位，继续编码下一个字节 */
        value = (uint16)(value >> 7);
        if (value != 0)
        {
            b |= 0x80u; // 高位 1：后面还有字节（支持 0x800E 等大 module_id）
        }

        /* 检查是否超出输出缓冲区大小 */
        if (idx >= out_size)
        {
            return 0;
        }

        /* 将编码后的字节写入输出缓冲区，并递增索引 */
        out[idx++] = b;
    } while (value != 0);

    return idx;
}

/* FF01 body 从索引 1 起追加一节 TLV；buf[0] 由调用方事后写模块区总长 */
static int evt_ble_pack_varint_module(uint8 *out_buf, uint16 buf_size, uint16 *offset,
                                      uint16 module_id, const uint8 *data, uint16 len)
{
    uint8 id_varint[3] = {0};
    uint8 len_varint[3] = {0};
    uint8 id_len;
    uint8 l_len;
    uint16 need;

    if (out_buf == NULL || offset == NULL || (len > 0 && data == NULL))
    {
        return -1;
    }

    // TLV 头两段变长：ble_comu_def 中的 UTC_TIMESTAMP / EVENT_DAY_STAT 等
    id_len = evt_ble_encode_varint_u16(module_id, id_varint, sizeof(id_varint));
    l_len = evt_ble_encode_varint_u16(len, len_varint, sizeof(len_varint));
    if (id_len == 0 || l_len == 0)
    {
        return -1;
    }

    need = (uint16)(id_len + l_len + len); // 本节总占用 = id + len 编码 + payload
    if ((uint16)(*offset + need) > buf_size)
    {
        return -1;
    }

    memcpy(&out_buf[*offset], id_varint, id_len); // [module_id varint]
    *offset = (uint16)(*offset + id_len);
    memcpy(&out_buf[*offset], len_varint, l_len); // [len varint]
    *offset = (uint16)(*offset + l_len);
    if (len > 0)
    {
        memcpy(&out_buf[*offset], data, len); // [payload]，len==0 时不拷贝
        *offset = (uint16)(*offset + len);
    }
    return 0;
}

/* EVENT_DAY_STAT(0x800E)：固定 28B，大端 */
static uint16 evt_ble_build_day_800e_payload(const my_ems_day_wire_t *day, uint8 *out, uint16 out_size)
{
    uint16 offset = 0;

    if (day == NULL || out == NULL || out_size < 28)
    {
        return 0;
    }

    offset += evt_ble_w32be(&out[offset], day->day_unix_utc0); // offset 0，4B：当日零点 UTC 时间戳（秒）
    offset += evt_ble_w16be(&out[offset], day->chg_min_total); // 4，2B：充电分钟累计（预留）
    offset += evt_ble_w16be(&out[offset], day->dsg_min_total); // 6，2B：放电分钟累计（预留）
    offset += evt_ble_w16be(&out[offset], 0);                  // 8，2B：预留
    offset += evt_ble_w16be(&out[offset], 0);                  // 10，2B：预留
    offset += evt_ble_w16be(&out[offset], 0);                  // 12，2B：预留（三路放电分钟等，现填 0）
    offset += evt_ble_w16be(&out[offset], day->cnt_power);     // 14，2B：电源输出开关计数
    offset += evt_ble_w16be(&out[offset], day->cnt_inv);       // 16，2B：逆变输出开关计数
    offset += evt_ble_w16be(&out[offset], day->cnt_usb);       // 18，2B：USB 输出开关计数
    offset += evt_ble_w16be(&out[offset], day->cnt_dc5521);    // 20，2B：DC5521 输出开关计数
    offset += evt_ble_w16be(&out[offset], day->cnt_led);       // 22，2B：LED 输出开关计数
    offset += evt_ble_w16be(&out[offset], 0);                  // 24，2B：末尾预留
    offset += evt_ble_w16be(&out[offset], 0);                  // 26，2B：末尾预留
    return offset;
}

/* 打印 FF01/5C01 明文 */
#if MY_EMS_BLE_PROTO_LOG_EN
#define EVT_BLE_EMS_LOG_BUF_MAX 256
static void evt_ble_proto_log_snapshot(const my_ems_day_wire_t *day)
{
    uint8 log_buf[EVT_BLE_EMS_LOG_BUF_MAX] = {0};
    uint8 mod09[4] = {0};    // 4B UTC_TIMESTAMP
    uint8 mod800e[28] = {0}; // 28B EVENT_DAY_STAT
    uint16 off = 0;          // body[0] 预留给「后续 module 区总长」，不含自身 1 字节
    uint16 raw_len = 0;      // 原始长度
    uint16 pad_len = 0;      // 16 字节对齐 padding
    uint16 idx = 0;          // 告警序号
    uint16 n = 0;            // 告警数量
    uint16 pay_lim = 0;      // 最大载荷

    if (day == NULL || day->day_unix_utc0 == 0)
    {
        return;
    }

    // ------- BLE_DATA_TYPE_EXPANSION_MODULE(0xFF01) 扩展模块明文 body -------
    memset(log_buf, 0, sizeof(log_buf));
    off = 1; // body[0] 预留给「后续 module 区总长」，不含自身 1 字节

    // 步骤 A：TLV — UTC_TIMESTAMP，payload 为 4B「当日零点 UTC 时间戳(day_unix_utc0)」大端
    evt_ble_w32be(mod09, day->day_unix_utc0);
    if (evt_ble_pack_varint_module(log_buf, sizeof(log_buf), &off, UTC_TIMESTAMP, mod09, sizeof(mod09)) != 0)
    {
        my_log_printf(1, "[EMS] ble log pack UTC fail");
        return;
    }

    // 步骤 B：先组 EVENT_DAY_STAT(0x800E) 28B，再作为第二段 TLV 的 payload 追加
    raw_len = evt_ble_build_day_800e_payload(day, mod800e, sizeof(mod800e));
    if (raw_len != 28)
    {
        my_log_printf(1, "[EMS] ble log 800E len %u", raw_len);
        return;
    }
    if (evt_ble_pack_varint_module(log_buf, sizeof(log_buf), &off, EVENT_DAY_STAT, mod800e, 28) != 0)
    {
        my_log_printf(1, "[EMS] ble log pack 800E fail");
        return;
    }

    // 步骤 C：body[0]=body[1..] 总长；打 raw 与 16 字节对齐 padding（余字节填 0）
    log_buf[0] = (uint8)(off - 1);
    raw_len = off;
    pad_len = evt_ble_plain_round_up_16(raw_len);
    my_log_printf(1, "[EMS] FF01 plaintext (store) raw=%u pad16=%u type=0x%04X", raw_len, pad_len,
                  BLE_DATA_TYPE_EXPANSION_MODULE);
    put_buf((u8 *)log_buf, raw_len);
    if (pad_len > raw_len)
    {
        put_buf((u8 *)&log_buf[raw_len], pad_len - raw_len);
    }

    if (s_alarm_evt.count == 0)
    {
        return;
    }

    // ------- BLE_DATA_TYPE_STD_ALARM_REPORT(0x5C01) -------
    memset(log_buf, 0, sizeof(log_buf));
    off = EVT_STD_ALARM_HDR_SZ;        // 先占满 3 字节帧头，告警数据从 log_buf[3] 起写
    log_buf[0] = BLE_STD_REPORT_START; // 0x8A，与 ble_comu_def 中 BLE_STD_REPORT_START 一致
    log_buf[2] = BLE_COMU_DEV_CODE;    // 设备端识别字节
    // buf[1]：写满载荷后再填 = off-2，表示 buf[2]～buf[off-1] 共多少字节（dev_code + 告警条目）
    pay_lim = (uint16)(EVT_STD_ALARM_HDR_SZ + EVT_STD_ALARM_PL_MAX); // 头 + 最大载荷，防超长
    idx = s_alarm_evt.head;
    for (n = 0; n < s_alarm_evt.count; n++)
    {
        if ((uint16)(off + EVT_STD_ALARM_ENTRY_SZ) > sizeof(log_buf) ||
            (uint16)(off + EVT_STD_ALARM_ENTRY_SZ) > pay_lim)
        {
            break; // local buf 或 EVT_STD_ALARM_PL_MAX 限制
        }
        off += evt_ble_w32be(&log_buf[off], s_alarm_evt.ring[idx].ts_unix);      // 4B BE
        off += evt_ble_w16be(&log_buf[off], (uint16)s_alarm_evt.ring[idx].kind); // 2B BE，故障位号
        idx = (uint16)((idx + 1) % MY_EVT_ALARM_CAP);                            // FIFO：自 head 起先旧后新
    }
    if (off <= EVT_STD_ALARM_HDR_SZ)
    {
        return;
    }
    log_buf[1] = (uint8)(off - 2);
    raw_len = off;
    pad_len = evt_ble_plain_round_up_16(raw_len);
    my_log_printf(1, "[EMS] 5C01 plaintext (store fifo n=%u) raw=%u pad16=%u", (unsigned)s_alarm_evt.count,
                  raw_len, pad_len);
    put_buf((u8 *)log_buf, raw_len);
    if (pad_len > raw_len)
    {
        put_buf((u8 *)&log_buf[raw_len], pad_len - raw_len);
    }
}
#endif

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
    memset(&s_evt_tx, 0, sizeof(s_evt_tx));
    s_alarm_evt.seq_next = 1;
    s_day_vm_need_commit = 0;
    s_alarm_vm_need_commit = 0;
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
#if MY_EMS_BLE_PROTO_LOG_EN
            evt_ble_proto_log_snapshot(today_wire);
#endif
        }
        s_prev_alarm_bits = cur;
    }
}

/** 立即写入两块 EMS 数据块 */
void my_evt_flush_vm(void)
{
    evt_save_day_vm();
    evt_save_alarm_vm();
    s_day_vm_need_commit = 0;
    s_alarm_vm_need_commit = 0;
}

/** 告警环形队列（FIFO）当前有效条数：与 head/tail/count 一致；容量满时策略为丢最旧 */
int my_evt_alarm_pending_count(void)
{
    return s_alarm_evt.count;
}

/** 删除单条 day 记录：仅 RAM 删除，延后到会话收尾时统一写 VM */
static int evt_day_drop_one(uint8 day_idx)
{
    /* 参数检查。 */
    if (day_idx >= MY_EVT_DAY_SLOT_MAX)
    {
        return -1;
    }

    /* 已删除的记录直接返回。 */
    if (s_day_evt.records[day_idx].day_unix_utc0 == 0)
    {
        return 0;
    }

    /* 先只从 RAM 删除，延后到会话收尾时统一提交 VM。 */
    evt_day_record_reset(&s_day_evt.records[day_idx]);
    s_day_vm_need_commit = 1;
    return 1;
}

/** 删除一批告警记录：仅 RAM 删除，延后到会话收尾时统一写 VM */
static int evt_alarm_drop_batch(uint16 count)
{
    uint16 drop = count;

    /* 参数检查。 */
    if (drop == 0 || s_alarm_evt.count == 0)
    {
        return -1;
    }

    /* 删除数量不能超过当前 FIFO 有效数量。 */
    if (drop > s_alarm_evt.count)
    {
        return -1;
    }

    /* 只推进 RAM 头指针；VM 留到会话收尾时统一提交。 */
    s_alarm_evt.head = (uint16)((s_alarm_evt.head + drop) % MY_EVT_ALARM_CAP);
    s_alarm_evt.count = (uint16)(s_alarm_evt.count - drop);
    s_alarm_vm_need_commit = 1;
    return drop;
}

/** 会话结束/断开/失败收尾时，真正写 flash VM */
static void evt_commit_vm_if_needed(void)
{
    /* 只有会话结束/断开/失败收尾时才真正写 flash，避免每个 ACK 都擦写。 */
    if (s_day_vm_need_commit)
    {
        evt_save_day_vm();
        s_day_vm_need_commit = 0;
    }

    if (s_alarm_vm_need_commit)
    {
        evt_save_alarm_vm();
        s_alarm_vm_need_commit = 0;
    }
}

/** 连接上报会话：开始/结束（结束时会对 day/alarm 的 RAM 删除统一提交 VM） */
void my_evt_tx_session_start(void)
{
    memset(&s_evt_tx, 0, sizeof(s_evt_tx));
    s_evt_tx.started = 1;
}

/** 连接上报会话：结束（结束时会对 day/alarm 的 RAM 删除统一提交 VM） */
void my_evt_tx_session_stop(void)
{
    /* 无论是全发完、ACK 失败还是断连，统一在这里提交已删除的 RAM 改动。 */
    evt_commit_vm_if_needed();
    memset(&s_evt_tx, 0, sizeof(s_evt_tx));
}

/** 组包下一条 day 扩展上报（FF01，包含 0x09 + 0x800E）：
 * - 返回 1：已生成一包，等待 ACK
 * - 返回 0：当前无可发送 day 包
 */
int my_evt_prepare_next_day_expansion_packet(uint8 *out_buf, uint16 buf_size, uint16 *out_len)
{
    uint8 mod09[4] = {0};
    uint8 mod800e[28] = {0};
    uint16 off = 1;
    uint16 payload_len = 0;
    uint8 idx;
    my_ems_day_wire_t *day;

    if (out_len != NULL)
    {
        *out_len = 0;
    }

    /* 参数检查。 */
    if (!s_evt_tx.started || out_buf == NULL || out_len == NULL || buf_size < 2)
    {
        return 0;
    }

    /* 等待 ACK 或已停止时不组包。 */
    if (s_evt_tx.day_wait_ack || s_evt_tx.day_stopped)
    {
        return 0;
    }

    /* 从当前指针向后扫描，找到第一条有效 day 记录。 */
    while (s_evt_tx.day_cursor < MY_EVT_DAY_SLOT_MAX)
    {
        idx = s_evt_tx.day_cursor;
        day = &s_day_evt.records[idx];
        if (day->day_unix_utc0 == 0)
        {
            s_evt_tx.day_cursor++;
            continue;
        }

        /* FF01 一包只带一条 day：0x09(UTC) + 0x800E(日统计)。 */
        evt_ble_w32be(mod09, day->day_unix_utc0);
        if (evt_ble_pack_varint_module(out_buf, buf_size, &off, UTC_TIMESTAMP, mod09, sizeof(mod09)) != 0)
        {
            return 0;
        }

        /* 组 EVENT_DAY_STAT(0x800E) 28B，再作为第二段 TLV 的 payload 追加。 */
        payload_len = evt_ble_build_day_800e_payload(day, mod800e, sizeof(mod800e));
        if (payload_len != sizeof(mod800e))
        {
            return 0;
        }
        /* 组第二段 TLV。 */
        if (evt_ble_pack_varint_module(out_buf, buf_size, &off, EVENT_DAY_STAT, mod800e, payload_len) != 0)
        {
            return 0;
        }

        /* 组包结束，更新包头长度。 */
        out_buf[0] = (uint8)(off - 1);
        *out_len = off;
        /* 记录当前记录项，并进入等待 ACK 状态。 */
        s_evt_tx.day_last_sent = idx;
        s_evt_tx.day_wait_ack = 1;
        my_log_printf(1, "[EMS] tx day packet idx=%u day=0x%08lx len=%u",
                      (unsigned)idx, (unsigned long)day->day_unix_utc0, (unsigned)*out_len);
        return 1;
    }

    return 0;
}

/** 处理 day 扩展上报 ACK：
 * - is_ok=1：删除当前记录项，继续扫描后续 day
 * - is_ok=0：停止 day 阶段并等待统一提交
 * 返回 1 表示仍可继续扫描 day，返回 0 表示 day 阶段结束或停止
 */
int my_evt_on_day_expansion_ack(uint8 is_ok)
{
    int drop_ret;

    /* 检查会话是否已开始，且当前有等待 ACK 的 day 包。 */
    if (!s_evt_tx.started || !s_evt_tx.day_wait_ack)
    {
        return 0;
    }

    /* 设置 day 等待 ACK 标志为 0，表示当前 day 包已处理。 */
    s_evt_tx.day_wait_ack = 0;
    if (!is_ok)
    {
        /* day ACK 失败时停止 day 阶段；已删条目会在会话收尾时统一提交 VM。 */
        s_evt_tx.day_stopped = 1;
        my_log_printf(1, "[EMS] tx day ack fail");
        return 0;
    }

    /* 只有 ACK 成功，才删除刚才发出的那个记录项。 */
    drop_ret = evt_day_drop_one(s_evt_tx.day_last_sent);
    my_log_printf(1, "[EMS] tx day ack ok idx=%u drop=%d", (unsigned)s_evt_tx.day_last_sent, drop_ret);
    s_evt_tx.day_cursor++;
    return 1;
}

/** 组包下一条告警标准上报（5C01，可多条）：
 * - 返回 1：已生成一包，等待 ACK
 * - 返回 0：当前无可发送告警包
 */
int my_evt_prepare_next_alarm_packet(uint8 *out_buf, uint16 buf_size, uint16 *out_len)
{
    my_ems_alarm_wire_t alarms[MY_EVT_ALARM_CAP];
    uint16 total;
    uint16 i;
    uint16 off = EVT_STD_ALARM_HDR_SZ;
    uint16 pay_lim;
    uint16 sent = 0;

    if (out_len != NULL)
    {
        *out_len = 0;
    }
    if (!s_evt_tx.started || out_buf == NULL || out_len == NULL || buf_size < EVT_STD_ALARM_HDR_SZ)
    {
        return 0;
    }
    if (s_evt_tx.alarm_wait_ack)
    {
        return 0;
    }

    out_buf[0] = BLE_STD_REPORT_START;
    out_buf[1] = 0;
    out_buf[2] = BLE_COMU_DEV_CODE;

    pay_lim = buf_size;
    if (pay_lim > (uint16)(EVT_STD_ALARM_HDR_SZ + EVT_STD_ALARM_PL_MAX))
    {
        pay_lim = (uint16)(EVT_STD_ALARM_HDR_SZ + EVT_STD_ALARM_PL_MAX);
    }

    /* 5C01 一包可带多条告警，这里先从 FIFO 头部窥视一个窗口出来组包。 */
    total = (uint16)evt_alarm_peek_batch(alarms, MY_EVT_ALARM_CAP);
    for (i = 0; i < total; i++)
    {
        if ((uint16)(off + EVT_STD_ALARM_ENTRY_SZ) > pay_lim)
        {
            break;
        }
        off += evt_ble_w32be(&out_buf[off], alarms[i].ts_unix);
        off += evt_ble_w16be(&out_buf[off], (uint16)alarms[i].kind);
        sent++;
    }

    if (sent == 0)
    {
        return 0;
    }

    out_buf[1] = (uint8)(off - 2);
    *out_len = off;
    /* 记录当前包带了多少条，后续 ACK 成功时按这个窗口从 FIFO 头部删除。 */
    s_evt_tx.alarm_last_sent = sent;
    s_evt_tx.alarm_wait_ack = 1;
    my_log_printf(1, "[EMS] tx alarm packet n=%u pending=%u len=%u",
                  (unsigned)sent, (unsigned)s_alarm_evt.count, (unsigned)*out_len);
    return 1;
}

/** 处理告警 ACK：
 * - is_ok=1：删除本包窗口并决定是否继续
 * - is_ok=0：停止并等待统一提交
 * 返回 1 表示可继续发送下一包告警，返回 0 表示告警阶段结束/停止
 */
int my_evt_on_alarm_report_ack(uint8 is_ok)
{
    int drop_ret;

    /* 检查会话是否已开始，且当前有等待 ACK 的告警包 */
    if (!s_evt_tx.started || !s_evt_tx.alarm_wait_ack)
    {
        return 0;
    }

    /* 设置告警等待 ACK 标志为 0，表示当前告警包已处理 */
    s_evt_tx.alarm_wait_ack = 0;
    if (!is_ok)
    {
        /* alarm ACK 失败时停止继续发送；已成功删除的窗口留到会话收尾时统一 commit。 */
        my_log_printf(1, "[EMS] tx alarm ack fail");
        s_evt_tx.alarm_last_sent = 0;
        return 0;
    }

    /* 只有 ACK 成功，才真正从 FIFO 头部删除本包对应窗口。 */
    drop_ret = evt_alarm_drop_batch(s_evt_tx.alarm_last_sent);
    my_log_printf(1, "[EMS] tx alarm ack ok drop=%d left=%u",
                  drop_ret, (unsigned)s_alarm_evt.count);
    s_evt_tx.alarm_last_sent = 0;
    return (s_alarm_evt.count > 0) ? 1 : 0;
}

uint8 my_evt_day_vm_magic_ok(void)
{
    return evt_check_vm_magic(CFG_EMS_DAY_STATS_BLOB, MY_DEP_EVT_BLOB_MAGIC);
}

uint8 my_evt_alarm_vm_magic_ok(void)
{
    return evt_check_vm_magic(CFG_EMS_ALARM_FIFO_BLOB, MY_DEP_ALARM_BLOB_MAGIC);
}
