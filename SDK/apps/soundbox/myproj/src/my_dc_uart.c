/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : my_dc_uart.c
 * Brief      : DC板UART通信模块
 * Version    : V1.0.0
 * Author     : songshiqing(songshiqing@jimiiot.com)
 * Date       : 2026-03-02
 ************************************************************************************************/

#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".my_dc_uart.data.bss")
#pragma data_seg(".my_dc_uart.data")
#pragma const_seg(".my_dc_uart.text.const")
#pragma code_seg(".my_dc_uart.text")
#endif

#include "my_common.h"

/*
 * ============================================================================
 * 【协议文档】Voltana Go 3 EMS Modbus V1.2
 * ============================================================================
 *
 * 1. 通讯参数：
 *    - TTL，1 起始位，8 数据位，1 停止位，无校验，波特率 115200
 *    - 装置地址固定 0x01
 *
 * 2. 功能码：
 *    | 03 | 读寄存器 | 查询/主动上报整表 |
 *    | 05 | 设置寄存器 0x0001 | 写入完整开关寄存器值 |
 *
 * 3. 寄存器 0x0001（读写，05 写完整字）摘要：
 *    Bit0 开机 | Bit1 逆变 | Bit2 USB | Bit3 DC5521 | Bit4 LED | Bit5 蓝牙图标
 *    Bit6 逆变休眠 | Bit7 整机休眠 | Bit8 运输模式 | Bit9 解除蓝牙双模连接 | Bit10 频率 50/60Hz
 *    （告警等只读位见 0x0002/0x0003，与 0x0001 开关位分离）
 *    | 0x0004 | 充电功率 0.1W | 0x0007 电池SOC 0.1% | 0x0012 DC5521功率 |
 *    | 0x0013 | LED功率 | 0x000E 逆变器功率 | 0x000F USB总功率 |
 *
 * 4. CRC16：初值 0xFFFF，多项式 0xA001，最后高低字节交换
 * ============================================================================
 */

/* ========== Modbus 协议参数（协议 3.2.1、3.2.2） ========== */
#define DC_MODBUS_ADDR           0x01 /* 装置地址固定 0x01 */
#define DC_MODBUS_FUNC_READ_REG  0x03 /* 读寄存器 */
#define DC_MODBUS_FUNC_WRITE_REG 0x05 /* 按协议约定写 0x0001 完整寄存器 */

/* ========== 时序参数（协议 3.1：从机最大回复时间 500ms） ========== */
#define DC_MODBUS_RESP_TIMEOUT_MS    500  /* 从机最大回复时间：超时则本次请求失败，置失败 */
#define DC_OFFLINE_CONSECUTIVE_COUNT 3    /* 连续多少次超时判离线 */
#define DC_SCHED_TICK_MS             100  /* DC 调度 tick：init 后定时器常驻 */
#define DC_SESSION_POLL_INTERVAL_MS  1000 /* 轮询间隔 */

/*
 * 业务关注的寄存器地址（寄存器点表）
 * 实际值=寄存器值/10（系数 0.1），单位见协议
 */
#define DC_REG_SWITCH_FLAGS        0x0001 /* 开关量寄存器；05 写入完整 0x0001 寄存器值 */
#define DC_REG_SWITCH_BIT_AC       1      /* 逆变器开关 */
#define DC_REG_SWITCH_BIT_USB      2      /* USB 开关 */
#define DC_REG_SWITCH_BIT_DC5521   3      /* DC5521 开关 */
#define DC_REG_SWITCH_BIT_LED      4      /* LED 开关 */
#define DC_REG_SWITCH_BIT_BLE_ICON 5      /* V1.2 2.2：蓝牙图标开关位 */

#define DC_REG_FAULT_STATUS    0x0003 /* 故障状态(只读) Bit4 逆变过载 Bit8 逆变过温 Bit9 电池过温 */
#define DC_REG_CHARGE_POWER    0x0004 /* 充电功率 0.1W */
#define DC_REG_BAT_SOC         0x0007 /* 电池SOC 0.1% */
#define DC_REG_REMAIN_TIME     0x0009 /* 剩余可用时间 0.1H */
#define DC_REG_BAT_TEMP        0x000B /* 电池温度 0.1℃ */
#define DC_REG_TOTAL_POWER     0x000D /* 输出总功率 0.1W */
#define DC_REG_AC_POWER        0x000E /* 逆变器功率 0.1W */
#define DC_REG_USB_TOTAL_POWER 0x000F /* USB总功率 0.1W */
#define DC_REG_DC5521_POWER    0x0012 /* DC5521输出功率 0.1W */
#define DC_REG_LED_POWER       0x0013 /* LED输出功率 0.1W */

/*
 * Voltana Go 3 协议 3.1 查询：01 03 00 00 00 28，读 0x0000 起连续 40 个保持寄存器至 0x0027
 * 主动上报只接受同样的整表 03 数据块（byte_cnt=80）
 */
#define DC_03_POLL_START    0x0000
#define DC_03_POLL_QTY      0x0028                        /* 40 */
#define DC_03_POLL_BYTE_CNT ((uint8)((DC_03_POLL_QTY)*2)) /* 80 */

/* 寄存器镜像：0x0000~0x0027 */
#define DC_REG_CACHE_SIZE (DC_03_POLL_START + DC_03_POLL_QTY)

/* 接收拼包缓存、单帧发送缓存 */
#define DC_RX_CACHE_SIZE 128
#define DC_TX_FRAME_MAX  16

/* 在途 Modbus 请求上下文：同一时刻仅允许一个请求 */
typedef struct
{
    uint8 func;        /* 功能码 */
    uint16 start_addr; /* 起始地址 */
    uint16 quantity;   /* 寄存器个数 */
    uint16 value;      /* 写入值 */
    uint16 wait_ms;    /* 等待时间 */
    uint8 active;      /* 是否活动 */
} dc_req_ctx_t;

/* 排队中的开关控制请求：由 my_dc_ctrl_switch 写入，dc_try_send_ctrl_req 消费 */
typedef struct
{
    uint8 valid;             /* 是否有效 */
    my_dc_sw_id_t sw_id;     /* 开关ID */
    uint8 onoff;             /* 目标开关状态 */
    uint16 target_reg_value; /* 目标 0x0001 完整寄存器值 */
    uint8 waiting_confirm;   /* 控制事务待 03 回读确认 */
} dc_ctrl_req_t;

/* ========== 寄存器与设备数据 ========== */
static uint16 g_dc_reg_cache[DC_REG_CACHE_SIZE] = {0}; /* Modbus 寄存器镜像 */
static device_data g_dc_dev_data = {0};                /* 业务数据快照 */
static uint8 g_dc_switch_reg_ready = 0;                /* 已拿到可信的 0x0001 寄存器基线，可据此计算 01 05 目标值 */

/* ========== 链路健康状态 ========== */
static uint8 g_dc_online = 0;                             /* 链路健康：DC_OFFLINE_CONSECUTIVE_COUNT 次超时置 0 */
static uint8 g_dc_timeout_count = 0;                      /* 连续超时计数，>= DC_OFFLINE_CONSECUTIVE_COUNT 时 g_dc_online=0 */
static uint8 g_dc_last_ctrl_result = MY_DC_CTRL_RET_IDLE; /* 最近一次开关控制结果 */
static uint8 g_dc_last_ctrl_err = 0;                      /* 异常应答时的错误码 */

/* ========== 请求上下文 ========== */
static dc_req_ctx_t g_dc_req = {0};       /* 在途 Modbus 请求，同一时刻仅一个 */
static dc_ctrl_req_t g_dc_ctrl_req = {0}; /* 排队中的开关控制请求 */

/* ========== 发送 DMA 缓冲（UART 驱动需 DMA 可访问内存） ========== */
static uint8 *g_dc_tx_dma_buf = NULL; /* init 时分配，deinit 时释放 */

/* ========== 轮询与调度状态 ========== */
static uint32 g_dc_tick_ms = 0;               /* 软件 tick，仅定时器运行时累加 */
static uint32 g_dc_last_poll_dispatch_ms = 0; /* 上次发起轮询的时刻，dc_poll_stop 时重置 */
static uint8 g_dc_timer_running = 0;          /* 1=DC 调度定时器运行（init 后常开） */

#define DC_LOG_FRAME_EN   0 /* 协议层提交：开启帧打印便于 review */
#define DC_LOG_VERBOSE_EN 0 /* 开启解析/上报日志 */

#define MY_DC_MOCK_EN 0 // 模拟数据，不依赖真实 DC 板

#if DC_LOG_VERBOSE_EN
#define DC_LOG_VERBOSE(...) my_log_printf(1, __VA_ARGS__)
#else
#define DC_LOG_VERBOSE(...)
#endif

/* 调试用：打印 Modbus 帧 */
static void dc_log_frame(const char *tag, const uint8 *buf, uint16 len)
{
#if DC_LOG_FRAME_EN
    if (tag == NULL || buf == NULL || len == 0)
    {
        return;
    }

    my_log_printf(1, "%s len=%d", tag, len);
    put_buf((u8 *)buf, len);
#else
    (void)tag;
    (void)buf;
    (void)len;
#endif
}

static void dc_poll_timer_cb(void *param);
static void dc_confirm_ctrl_by_switch_flags(uint16 reg_switch_flags);

/*
 * ============================================================================
 * 【协议层】Modbus 编解码
 * ============================================================================
 */

/* Modbus CRC16（协议 3.4） */
static uint16 dc_modbus_crc16(const uint8 *data, uint16 len)
{
    uint16 crc = 0xFFFF;
    uint16 i = 0;
    uint8 bit = 0;

    for (i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++)
        {
            if (crc & 0x0001)
            {
                crc >>= 1;
                crc ^= 0xA001;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    /* 协议要求：返回时高低字节交换 */
    return (uint16)((crc >> 8) | (crc << 8));
}

/* 检查寄存器地址是否在缓存范围内（0 ~ DC_REG_CACHE_SIZE-1），防止越界 */
static int dc_reg_addr_valid(uint16 addr)
{
    return (addr < DC_REG_CACHE_SIZE);
}

/* 写入寄存器镜像，地址非法则忽略 */
static void dc_set_reg_value(uint16 addr, uint16 val)
{
    if (!dc_reg_addr_valid(addr))
    {
        return;
    }

    g_dc_reg_cache[addr] = val;
}

/* 读取寄存器镜像，地址非法返回 def */
static uint16 dc_get_reg_value(uint16 addr, uint16 def)
{
    if (!dc_reg_addr_valid(addr))
    {
        return def;
    }

    return g_dc_reg_cache[addr];
}

/*
 * 从寄存器镜像更新 device_data（协议 2.1 系数 0.1）
 * 寄存器->业务：SOC/10->%，remain*6->min(0.1H*60)，温度/功率 0.1 单位
 * 开关状态：0x0001 Bit1=逆变 Bit2=USB Bit3=DC Bit4=LED -> sw_status bit0~3
 */
static void dc_update_device_data_cache(void)
{
    /* 读取寄存器并做单位换算 */
    uint16 reg_soc = dc_get_reg_value(DC_REG_BAT_SOC, 0);
    uint16 reg_remain = dc_get_reg_value(DC_REG_REMAIN_TIME, 0);
    uint16 reg_sw = dc_get_reg_value(DC_REG_SWITCH_FLAGS, 0);

    g_dc_dev_data.bat_percent = (uint8)MIN((reg_soc / 10), 100);
    g_dc_dev_data.remain_time = (uint16)MIN((reg_remain * 6), 0xFFFF);
    g_dc_dev_data.bat_temp = dc_get_reg_value(DC_REG_BAT_TEMP, 0);
    g_dc_dev_data.output_total_power = dc_get_reg_value(DC_REG_TOTAL_POWER, 0);
    g_dc_dev_data.input_total_power = dc_get_reg_value(DC_REG_CHARGE_POWER, 0);
    g_dc_dev_data.ac_power = dc_get_reg_value(DC_REG_AC_POWER, 0);
    g_dc_dev_data.dc_power = dc_get_reg_value(DC_REG_DC5521_POWER, 0);
    g_dc_dev_data.usb_power = dc_get_reg_value(DC_REG_USB_TOTAL_POWER, 0);
    g_dc_dev_data.led_power = dc_get_reg_value(DC_REG_LED_POWER, 0);

    /* 协议2.2 0x0001寄存器映射到业务sw_status
     * Bit1=逆变→bit0、Bit3=DC5521→bit1、Bit2=USB→bit2、Bit4=LED→bit3
     */
    g_dc_dev_data.sw_status = 0;
    if (reg_sw & (1 << DC_REG_SWITCH_BIT_AC))
    {
        g_dc_dev_data.sw_status |= (1 << 0); /* 逆变器 */
    }

    if (reg_sw & (1 << DC_REG_SWITCH_BIT_DC5521))
    {
        g_dc_dev_data.sw_status |= (1 << 1); /* DC5521 */
    }

    if (reg_sw & (1 << DC_REG_SWITCH_BIT_USB))
    {
        g_dc_dev_data.sw_status |= (1 << 2); /* USB */
    }

    if (reg_sw & (1 << DC_REG_SWITCH_BIT_LED))
    {
        g_dc_dev_data.sw_status |= (1 << 3); /* LED */
    }

    /* 故障状态：0x0003 Bit4=逆变器过载 Bit8=逆变器过温 Bit9=电池过温 */
    uint16 reg_fault = dc_get_reg_value(DC_REG_FAULT_STATUS, 0);
    g_dc_dev_data.fault_status = 0;
    if (reg_fault & (1 << 4))
    {
        g_dc_dev_data.fault_status |= (1 << 0);
    }
    if (reg_fault & (1 << 8))
    {
        g_dc_dev_data.fault_status |= (1 << 1);
    }
    if (reg_fault & (1 << 9))
    {
        g_dc_dev_data.fault_status |= (1 << 2);
    }

    /* EMS：0x0001 开关节次 + 0x0003 告警上升沿 → RAM/VM（功率累计等后续再接） */
    my_evt_on_dc_sample(reg_sw, reg_fault);

    /* 确认控制结果 */
    dc_confirm_ctrl_by_switch_flags(reg_sw);
}

/*
 * 构造 Modbus RTU 请求帧（协议 3.2、3.3.1/3.3.2/3.3.3）
 * 帧格式：| 地址(1B) | 功能码(1B) | 起始地址(2B) | 数量/控制值(2B) | CRC(2B) |
 * - 03 读寄存器：addr=起始地址，quantity=寄存器个数
 * - 05 写 0x0001：addr=0x0001，value=完整寄存器值
 */
static uint16 dc_build_req_frame(uint8 *out, uint8 func, uint16 addr, uint16 quantity_or_value)
{
    uint16 crc = 0;
    uint16 len = 0;

    if (out == NULL)
    {
        return 0;
    }

    out[0] = DC_MODBUS_ADDR;
    out[1] = func;
    out[2] = (uint8)(addr >> 8);
    out[3] = (uint8)(addr & 0xFF);
    out[4] = (uint8)(quantity_or_value >> 8);
    out[5] = (uint8)(quantity_or_value & 0xFF);
    len = 6;

    crc = dc_modbus_crc16(out, len);
    out[len++] = (uint8)(crc >> 8);
    out[len++] = (uint8)(crc & 0xFF);

    return len;
}

/*
 * 根据帧头估算完整帧长度
 * - 异常应答(func|0x80)：5 字节
 * - 05 写应答：8 字节
 * - 03 读应答：5 + byte_cnt
 */
static uint8 dc_modbus_resp_expected_len_any(const uint8 *buf, uint16 len)
{
    if (len < 2)
    {
        return 0; /* 长度不足，无法判断 */
    }

    if ((buf[1] & 0x80) != 0)
    {
        return 5; /* 异常应答 */
    }

    if (buf[1] == DC_MODBUS_FUNC_WRITE_REG)
    {
        return 8; /* 05 固定 8 字节 */
    }

    if (buf[1] == DC_MODBUS_FUNC_READ_REG)
    {
        if (len < 3)
        {
            return 0;
        }
        return (uint8)(5 + buf[2]); /* 5 + byte_cnt */
    }

    return 0;
}

/*
 * 校验 Modbus 帧（协议 3.2）：地址=0x01、功能码合法、CRC 正确
 * 异常应答数据区为错误码（协议 3.2.3.2）：1=无效报文 2=地址/长度 3=数值无效 6=装置忙
 */
static int dc_modbus_check_frame(const uint8 *frame, uint16 frame_len)
{
    uint16 calc_crc = 0;
    uint16 rx_crc = 0;
    uint8 func = 0;

    if (frame == NULL || frame_len < 5)
    {
        return 0;
    }

    if (frame[0] != DC_MODBUS_ADDR)
    {
        return 0; /* 装置地址固定 0x01 */
    }

    func = (uint8)(frame[1] & 0x7F);
    if (func != DC_MODBUS_FUNC_READ_REG &&
        func != DC_MODBUS_FUNC_WRITE_REG)
    {
        return 0; /* 功能码非法 */
    }

    calc_crc = dc_modbus_crc16(frame, frame_len - 2);
    rx_crc = (uint16)((frame[frame_len - 2] << 8) | frame[frame_len - 1]);
    return (calc_crc == rx_crc) ? 1 : 0;
}

/* 业务开关 ID -> 0x0001 寄存器 bit 位 */
static uint8 dc_sw_to_reg_bit(my_dc_sw_id_t sw_id)
{
    switch (sw_id)
    {
        case MY_DC_SW_AC:
            return DC_REG_SWITCH_BIT_AC;
        case MY_DC_SW_DC:
            return DC_REG_SWITCH_BIT_DC5521;
        case MY_DC_SW_USB:
            return DC_REG_SWITCH_BIT_USB;
        case MY_DC_SW_LED:
            return DC_REG_SWITCH_BIT_LED;
        default:
            return 0xFF;
    }
}

/* 判断帧是否为某功能码的应答（协议 3.2.2：正常=func，异常=func|0x80） */
static int dc_is_expected_resp_frame(const uint8 *frame, uint8 func_expect)
{
    if (frame == NULL)
    {
        return 0;
    }

    return (frame[1] == func_expect || frame[1] == (uint8)(func_expect | 0x80));
}

/*
 * 解析 03 读寄存器应答（协议 3.3.2.2）
 * 格式 [addr|03|byte_cnt|reg_data...|CRC]，reg_data 大端序，每寄存器 2 字节
 */
static void dc_update_read_reg_cache(const uint8 *frame, uint16 start_addr)
{
    uint8 byte_cnt = 0;
    uint8 i = 0;
    uint16 reg_addr = 0;
    uint16 reg_val = 0;

    byte_cnt = frame[2];
    if (byte_cnt < 2 || (byte_cnt & 0x01))
    {
        return; /* 至少 2 字节且偶数 */
    }

    /* 从 start_addr 起依次解析，大端序：高字节在前 */
    for (i = 0; i < (byte_cnt / 2); i++)
    {
        reg_addr = (uint16)(start_addr + i);
        reg_val = (uint16)((frame[3 + i * 2] << 8) | frame[4 + i * 2]);
        dc_set_reg_value(reg_addr, reg_val);
    }
    DC_LOG_VERBOSE("dc parse reg ok. start=0x%x, reg_num=%d", start_addr, byte_cnt / 2);
    if (start_addr <= DC_REG_SWITCH_FLAGS && (uint16)(start_addr + (byte_cnt / 2)) > DC_REG_SWITCH_FLAGS)
    {
        g_dc_switch_reg_ready = 1;
    }
    dc_update_device_data_cache();
}

/*
 * 解析 03 主动上报：仅接受 Voltana 全表 80 字节
 */
static int dc_try_handle_03_report(const uint8 *frame, uint16 frame_len)
{
    (void)frame_len;
    if (frame[1] != DC_MODBUS_FUNC_READ_REG)
    {
        return 0;
    }

    if (frame[2] != DC_03_POLL_BYTE_CNT)
    {
        return 0;
    }

    dc_update_read_reg_cache(frame, DC_03_POLL_START);
    g_dc_online = 1;
    g_dc_timeout_count = 0;
    DC_LOG_VERBOSE("dc 03 report parsed. start=0x%x byte_cnt=%d", DC_03_POLL_START, frame[2]);
    return 1;
}

/*
 * ============================================================================
 * 【运行层】任务/定时器/调度逻辑
 * ============================================================================
 */

/* 启动 DC 轮询定时器（my_dc_uart_task init 成功后调用，常驻运行） */
static void dc_poll_start(void)
{
    if (g_dc_timer_running)
    {
        return; // 定时器已运行，直接返回
    }

    g_dc_timer_running = 1;
    my_start_timer(MY_TIMER_DC_POLL, DC_SCHED_TICK_MS, true, dc_poll_timer_cb);
    my_log_printf(1, "dc poll start");
}

/* 停止 DC 轮询定时器（仅 deinit 等资源释放场景） */
static void dc_poll_stop(void)
{
    if (!g_dc_timer_running)
    {
        return; // 定时器未运行，直接返回
    }

    g_dc_timer_running = 0;
    g_dc_tick_ms = 0;               // 下次连接时 tick 从 0 开始
    g_dc_last_poll_dispatch_ms = 0; // 下次连接时轮询节奏从新开始
    g_dc_req.active = 0;            // 清在途请求，否则重连后无法发新请求
    my_stop_timer(MY_TIMER_DC_POLL);
    my_log_printf(1, "dc poll stop");
}

/*
 * 发送 Modbus 请求（运行层）：构造帧、串口发送、填充 g_dc_req
 * 调用者：dc_try_send_ctrl_req, dc_send_ble_icon_once, dc_try_send_poll_req
 * 返回 0：链路忙或发送失败；返回 1：已发送，等待应答
 * 05 写 0x0001 完整寄存器值：start_addr=0x0001，value=目标寄存器值
 */
static int dc_send_request(uint8 func, uint16 start_addr, uint16 quantity, uint16 value)
{
    uint8 frame[16] = {0};
    uint16 frame_len = 0;
    uint16 payload = 0;
    uint32 send_len = 0;

    if (g_dc_req.active)
    {
        return 0; // 有在途请求，直接返回
    }

    if (func == DC_MODBUS_FUNC_READ_REG)
    {
        payload = quantity; // 读寄存器，payload 为 quantity
    }
    else
    {
        payload = value; // 写 0x0001，payload 为完整寄存器值
    }
    frame_len = dc_build_req_frame(frame, func, start_addr, payload);

    if (frame_len == 0)
    {
        return 0; // 构建请求帧失败，直接返回
    }

    dc_log_frame("dc tx frame", frame, frame_len);
    send_len = my_dc_uart_send(frame, frame_len);
    if (send_len != frame_len)
    {
        my_log_printf(1, "dc send fail. func=0x%x, addr=0x%x", func, start_addr);
        if (func == DC_MODBUS_FUNC_WRITE_REG)
        {
            g_dc_last_ctrl_result = MY_DC_CTRL_RET_SEND_FAIL; // 控制发送失败
            g_dc_last_ctrl_err = 0;                           // 无协议错误码
        }
        return 0; // 发送失败，直接返回
    }

    // 设置请求参数
    g_dc_req.func = func;
    g_dc_req.start_addr = start_addr;
    g_dc_req.quantity = quantity;
    g_dc_req.value = value;
    g_dc_req.wait_ms = 0;
    g_dc_req.active = 1;

    DC_LOG_VERBOSE("dc send ok. func=0x%x, addr=0x%x, qty=%d", func, start_addr, quantity);
    return 1;
}

/* 请求成功：读请求直接完成；写请求等待下一次常规 03 回读确认 */
static void dc_handle_req_result_ok(void)
{
    if (g_dc_req.func == DC_MODBUS_FUNC_WRITE_REG)
    {
        g_dc_ctrl_req.waiting_confirm = 1;
        g_dc_last_ctrl_result = MY_DC_CTRL_RET_PENDING;
        g_dc_last_ctrl_err = 0;
    }

    g_dc_online = 1;        // 置在线
    g_dc_timeout_count = 0; // 重置超时计数
    g_dc_req.active = 0;    // 清在途请求标志
    g_dc_req.wait_ms = 0;   // 重置等待时间
}

/* 请求失败（协议 3.2.3.2 异常应答）：err_code 1=无效报文 2=地址 3=数值 6=忙 */
static void dc_handle_req_result_fail(uint8 err_code)
{
    if (g_dc_req.func == DC_MODBUS_FUNC_WRITE_REG)
    {
        g_dc_ctrl_req.waiting_confirm = 0;
        g_dc_last_ctrl_result = MY_DC_CTRL_RET_EXCEPTION;
        g_dc_last_ctrl_err = err_code;
    }

    g_dc_req.active = 0;  // 清在途请求标志
    g_dc_req.wait_ms = 0; // 重置等待时间
    my_log_printf(1, "dc modbus exception. func=0x%x, err=0x%x", g_dc_req.func, err_code);
}

/*
 * 接收缓存解析（协议 3.2 帧格式，粘包/拆包）：
 * 1) 将新数据追加到内部缓存，溢出则清空
 * 2) 按帧头算期望长度，不足则等；算不出则丢 1 字节重同步
 * 3) 校验通过则尝试按从机主动上报解析，消费一帧后继续循环
 */
static void dc_try_parse_rx_cache(const uint8 *data, uint32 len)
{
    static uint8 rx_cache[DC_RX_CACHE_SIZE] = {0}; /* 串口接收粘包缓存，仅本函数使用 */
    static uint16 rx_len = 0;                      /* 缓存有效长度 */
    uint8 expected_len = 0;
    uint16 consume_len = 0;
    uint16 rx_addr = 0;
    uint16 rx_value = 0;

    /* 追加新数据 */
    if (data != NULL && len > 0)
    {
        if (len > (DC_RX_CACHE_SIZE - rx_len))
        {
            my_log_printf(1, "dc rx cache overflow, reset cache");
            rx_len = 0;
            memset(rx_cache, 0, sizeof(rx_cache));
            return;
        }
        memcpy(rx_cache + rx_len, data, len);
        rx_len += (uint16)len;
    }

    while (rx_len >= 5)
    {
        expected_len = dc_modbus_resp_expected_len_any(rx_cache, rx_len);
        if (expected_len == 0)
        {
            /* 无法识别帧头，丢 1 字节重同步 */
            memmove(rx_cache, rx_cache + 1, rx_len - 1);
            rx_len--;
            continue;
        }

        if (rx_len < expected_len)
        {
            break; /* 数据不足一帧，等待后续 */
        }

        if (!dc_modbus_check_frame(rx_cache, expected_len))
        {
            my_log_printf(1, "dc frame check fail, drop 1 byte");
            memmove(rx_cache, rx_cache + 1, rx_len - 1);
            rx_len--;
            continue;
        }

        dc_log_frame("dc rx frame", rx_cache, expected_len);
        if (g_dc_req.active && dc_is_expected_resp_frame(rx_cache, g_dc_req.func))
        {
            // 异常应答，处理异常
            if (rx_cache[1] == (uint8)(g_dc_req.func | 0x80))
            {
                dc_handle_req_result_fail(rx_cache[2]); // 异常应答，处理异常
            }
            // 读寄存器应答，处理读寄存器应答
            else if (g_dc_req.func == DC_MODBUS_FUNC_READ_REG)
            {
                /* 读寄存器应答，处理读寄存器应答 */
                if (rx_cache[2] == (uint8)(g_dc_req.quantity * 2))
                {
                    dc_update_read_reg_cache(rx_cache, g_dc_req.start_addr); // 更新读寄存器缓存
                    dc_handle_req_result_ok();                               // 处理读寄存器应答
                }
                else
                {
                    dc_handle_req_result_fail(2); // 读寄存器应答不匹配，处理异常
                }
            }
            // 05 应答：8 字节格式 01 05 00 01 [value_hi] [value_lo] CRC，成功要求地址和值回显一致
            else if (g_dc_req.func == DC_MODBUS_FUNC_WRITE_REG)
            {
                rx_addr = (uint16)((rx_cache[2] << 8) | rx_cache[3]);
                rx_value = (uint16)((rx_cache[4] << 8) | rx_cache[5]);
                if (rx_addr == DC_REG_SWITCH_FLAGS && rx_value == g_dc_req.value)
                {
                    dc_handle_req_result_ok();
                }
                else
                {
                    DC_LOG_VERBOSE("dc 05 resp mismatch. rx_addr=0x%x value=0x%x", rx_addr, rx_value);
                    dc_handle_req_result_fail(2);
                }
            }
        }
        else
        {
            /* 主动上报：仅保留 03 整表 */
            if (!dc_try_handle_03_report(rx_cache, expected_len))
            {
                DC_LOG_VERBOSE("dc unknown frame. func=0x%x", rx_cache[1]);
            }
        }

        /* 消费一帧，剩余数据前移 */
        consume_len = expected_len;
        if (consume_len < rx_len)
        {
            memmove(rx_cache, rx_cache + consume_len, rx_len - consume_len);
        }
        rx_len -= consume_len;
    }
}

/* 100ms 定时器回调：向 DC 任务发送 MY_MSG_DC_POLL_TICK，驱动轮询/超时/控制调度 */
static void dc_poll_timer_cb(void *param)
{
    (void)param;
    my_send_msg(MOD_DC_UART, MOD_DC_UART, MY_MSG_DC_POLL_TICK);
}

/*
 * 是否到了轮询时刻：
 * - 返回 1：定时器运行 且 距上次“成功发起轮询”已超过 DC_SESSION_POLL_INTERVAL_MS
 * - 返回 0：定时器未运行 或 间隔未到
 */
static int dc_need_poll_now(void)
{
    if (!g_dc_timer_running)
    {
        return 0; // 定时器停止，不轮询
    }

    if ((g_dc_tick_ms - g_dc_last_poll_dispatch_ms) < DC_SESSION_POLL_INTERVAL_MS)
    {
        return 0; // 距上次调度不足 interval
    }

    g_dc_last_poll_dispatch_ms = g_dc_tick_ms; // 本次允许调度，更新基准
    return 1;
}

/*
 * 在途请求超时检查（每 tick 调用）：
 * - 协议：从机回复一定在 500ms 内，否则本次请求视为失败
 * - 无在途请求：直接返回
 * - 有在途请求：累加 wait_ms，超 DC_MODBUS_RESP_TIMEOUT_MS 则置失败，
 *   g_dc_timeout_count++，>= DC_OFFLINE_CONSECUTIVE_COUNT 时 g_dc_online=0；
 *   若为写寄存器则 g_dc_last_ctrl_result=TIMEOUT
 */
static void dc_req_timeout_check(void)
{
    if (!g_dc_req.active)
    {
        return; /* 无在途请求，无需检查 */
    }

    g_dc_req.wait_ms += DC_SCHED_TICK_MS;
    if (g_dc_req.wait_ms < DC_MODBUS_RESP_TIMEOUT_MS)
    {
        return; /* 未超从机最大回复时间，继续等待 */
    }

    /* 已超时：置失败，清在途请求 */
    g_dc_req.active = 0;
    g_dc_req.wait_ms = 0;
    if (g_dc_timeout_count < 0xFF)
    {
        g_dc_timeout_count++;
    }

    // 连续超时达阈值，置离线
    if (g_dc_timeout_count >= DC_OFFLINE_CONSECUTIVE_COUNT)
    {
        g_dc_online = 0;
    }

    // 写 0x0001 超时，等待下一次常规 03 轮询确认真实状态
    if (g_dc_req.func == DC_MODBUS_FUNC_WRITE_REG)
    {
        g_dc_ctrl_req.waiting_confirm = 0;
        g_dc_last_ctrl_result = MY_DC_CTRL_RET_TIMEOUT;
        g_dc_last_ctrl_err = 0;
    }
}

/*
 * 尝试下发排队中的控制命令（写 0x0001）：
 * - 无有效控制请求：返回 0
 * - 目标值已在请求入队时计算完成；这里只负责发送
 * - 发送成功：清 g_dc_ctrl_req.valid，激活 g_dc_req，返回 1
 * - 发送失败（如已有在途请求）：返回 0，valid 保留，下次再试
 */
static int dc_try_send_ctrl_req(void)
{
    if (!g_dc_ctrl_req.valid)
    {
        return 0; // 无待发控制请求
    }

    if (!dc_send_request(DC_MODBUS_FUNC_WRITE_REG, DC_REG_SWITCH_FLAGS, 0,
                         g_dc_ctrl_req.target_reg_value))
    {
        return 0; // 发送失败（如 g_dc_req 已有在途请求）
    }

    g_dc_ctrl_req.valid = 0; // 已受理，清控制请求
    return 1;
}

/*
 * 定时查询：Voltana Go 3 协议 3.1 — 01 03 00 00 00 28
 */
static int dc_try_send_poll_req(void)
{
    return dc_send_request(DC_MODBUS_FUNC_READ_REG, DC_03_POLL_START, DC_03_POLL_QTY, 0) ? 1 : 0;
}

/* 返回 1：已发起蓝牙图标位写请求；0：未发送（忙/基线未就绪/无需变化） */
static int dc_send_ble_icon_once(uint8 on)
{
    uint16 cur = 0;
    uint16 target = 0;

    if (g_dc_req.active)
    {
        return 0;
    }

    /* 基线未就绪，不发送 */
    if (!g_dc_switch_reg_ready)
    {
        return 0;
    }

    /* 获取当前蓝牙图标位 */
    cur = dc_get_reg_value(DC_REG_SWITCH_FLAGS, 0);
    if (on)
    {
        target = (uint16)(cur | (uint16)(1u << DC_REG_SWITCH_BIT_BLE_ICON));
    }
    else
    {
        target = (uint16)(cur & (uint16)(~(1u << DC_REG_SWITCH_BIT_BLE_ICON)));
    }

    /* 目标值与当前值相同，不发送 */
    if (target == cur)
    {
        return 0;
    }

    /* 发送请求 */
    if (!dc_send_request(DC_MODBUS_FUNC_WRITE_REG, DC_REG_SWITCH_FLAGS, 0, target))
    {
        return 0;
    }
    return 1;
}

/*
 * 确认控制结果：通过 reg_switch_flags 与目标值对比，确认控制结果
 * - 目标值已在请求入队时计算完成；这里只负责确认
 * - 确认成功：结果置 OK
 * - 确认失败：结果置 EXCEPTION
 */
static void dc_confirm_ctrl_by_switch_flags(uint16 reg_switch_flags)
{
    /* 等待 03 回读确认 */
    if (!g_dc_ctrl_req.waiting_confirm)
    {
        return;
    }

    /* 确认成功 */
    if (reg_switch_flags == g_dc_ctrl_req.target_reg_value)
    {
        g_dc_last_ctrl_result = MY_DC_CTRL_RET_OK;
        g_dc_last_ctrl_err = 0;
    }
    /* 确认失败 */
    else
    {
        g_dc_last_ctrl_result = MY_DC_CTRL_RET_EXCEPTION;
        g_dc_last_ctrl_err = 3;
        DC_LOG_VERBOSE("dc 05 confirm mismatch. exp=0x%x got=0x%x",
                       g_dc_ctrl_req.target_reg_value, reg_switch_flags);
    }

    /* 清控制请求状态 */
    g_dc_ctrl_req.valid = 0;
    g_dc_ctrl_req.waiting_confirm = 0;
}

/*
 * 处理 POLL_TICK：更新 tick、超时检查、控制/轮询调度
 * - 定时器已停止（BLE 断开等）则不处理
 * - 更新软件 tick，供超时/轮询间隔计算
 * - 在途请求超时检查：可能置失败、离线
 * - 无在途请求时，优先发控制请求；无控制请求或发送失败时，距上次调度已满 interval 才发轮询
 */
static void dc_handle_poll_tick(void)
{
    if (!g_dc_timer_running)
    {
        return; // 定时器已停止（BLE 断开等），不处理 tick
    }

    g_dc_tick_ms += DC_SCHED_TICK_MS; // 更新软件 tick，供超时/轮询间隔计算
    dc_req_timeout_check();           // 在途请求超时检查：可能置失败、离线

    if (!g_dc_req.active) // 无在途请求时，才可发新请求
    {
        if (!dc_try_send_ctrl_req()) // 优先发控制请求；无控制请求或发送失败时再轮询
        {
            if (dc_need_poll_now()) // 距上次轮询调度已满 interval，才发轮询
            {
                dc_try_send_poll_req();
            }
        }
    }
}

void my_dc_uart_init(void)
{
    int ret = 0;
    MY_UART_ST_STRUCT param = {
        .mod = MOD_DC_UART,
        .port = MY_DC_UART_PORT,
        .tx_pin = MY_DC_UART_TX_PIN,
        .rx_pin = MY_DC_UART_RX_PIN,
        .baud = MY_DC_UART_BAUD,
    };

    ret = my_uart_init(&param);
    if (ret < 0)
    {
        my_log_printf(1, "dc uart init fail. ret=%d", ret);
        return;
    }

    if (g_dc_tx_dma_buf == NULL)
    {
        g_dc_tx_dma_buf = (uint8 *)dma_malloc(DC_TX_FRAME_MAX);
        if (g_dc_tx_dma_buf == NULL)
        {
            my_log_printf(1, "dc tx dma alloc fail");
        }
    }

    dc_poll_start(); // 初始化后，立即启动轮询

    DC_LOG_VERBOSE("dc uart init ok. port=%d", MY_DC_UART_PORT);
}

/**
 * 目前只有进入OTA模式，才会调用此函数，减少资源占用，提高OTA成功率
 * DC板通讯超时时间5min，避免OTA升级过程中无通讯导致复位
 */
void my_dc_uart_deinit(void)
{
    dc_poll_stop();

    if (g_dc_tx_dma_buf != NULL)
    {
        dma_free(g_dc_tx_dma_buf);
        g_dc_tx_dma_buf = NULL;
    }

    my_uart_deinit(MY_DC_UART_PORT);
}

uint32 my_dc_uart_send(uint8 *data, uint32 size)
{
    if (data == NULL || size == 0 || size > DC_TX_FRAME_MAX)
    {
        return 0;
    }

    if (g_dc_tx_dma_buf == NULL)
    {
        return 0;
    }

    memcpy(g_dc_tx_dma_buf, data, size);
    return my_uart_write_data(MY_DC_UART_PORT, g_dc_tx_dma_buf, size);
}

/* 串口接收入口：数据传入 dc_try_parse_rx_cache 做追加与解析 */
void my_dc_proto_feed(const uint8 *data, uint32 len)
{
    if (data == NULL || len == 0)
    {
        return;
    }

    dc_try_parse_rx_cache(data, len);
}

/* 获取设备数据快照；供 mb300_get_status_handler、my_send_all_status_handle 使用 */
int my_dc_get_data(device_data *out)
{
    if (out == NULL)
    {
        return -1;
    }

#if MY_DC_MOCK_EN
    /* 模拟数据：用于无 DC 板时的联调测试 */
    out->bat_percent = 85;
    out->remain_time = 360;        /* 6 小时 360min */
    out->bat_temp = 250;           /* 25.0℃ */
    out->output_total_power = 100; /* 10.0W */
    out->input_total_power = 50;   /* 5.0W */
    out->ac_power = 80;            /* 8.0W */
    out->dc_power = 20;            /* 2.0W */
    out->usb_power = 10;           /* 1.0W */
    out->led_power = 5;            /* 0.5W */
    out->sw_status = 0x0F;         /* 全部开关打开 */
    out->fault_status = 0x07;      /* bit0~bit2 全置1：逆变器过载/逆变器过温/电池过温 */
    return 0;
#else
    memcpy(out, &g_dc_dev_data, sizeof(device_data));
    return 0;
#endif
}

/* ========== DC 控制 ACK 机制（BLE 推送 RETURN_xx_xx_OK/FAIL） ========== */
#define DC_CTRL_ACK_CHECK_MS  DC_MODBUS_RESP_TIMEOUT_MS /* 与从机应答时限一致 */
#define DC_CTRL_ACK_MAX_CHECK 3

typedef struct
{
    uint8 valid;       // 是否有效
    uint8 check_count; // 轮询次数
    uint8 retry_count; // 失败重发次数，最多 DC_CTRL_ACK_MAX_CHECK 次
    char cmd[24];      // 命令
    char state[8];     // 状态
} dc_ctrl_ack_ctx_t;

static dc_ctrl_ack_ctx_t g_dc_ctrl_ack_ctx = {0};

/* 清除 ACK 上下文；dc_ctrl_ack_send_result 或启动定时器失败时调用 */
static void dc_ctrl_ack_ctx_clear(void)
{
    memset(&g_dc_ctrl_ack_ctx, 0, sizeof(g_dc_ctrl_ack_ctx));
}

/* 通过 BLE 主动推送控制结果：RETURN_<cmd>_<state>_OK 或 FAIL，并清除 ACK 上下文 */
static void dc_ctrl_ack_send_result(uint8 is_ok)
{
    char buf[64];
    uint16 cmd_type = BLE_DATA_TYPE_AT_CMD;
    int len;

    if (!g_dc_ctrl_ack_ctx.valid)
    {
        return;
    }

    if (check_connect_id_enable() == 0) // BLE 已断开则不再推送，避免无效发送
    {
        my_stop_timer(MY_TIMER_DC_CTRL_ACK);
        dc_ctrl_ack_ctx_clear();
        return;
    }

    len = snprintf(buf, sizeof(buf), "RETURN_%s_%s_%s",
                   g_dc_ctrl_ack_ctx.cmd, g_dc_ctrl_ack_ctx.state, is_ok ? "OK" : "FAIL");
    if (len > 0 && len <= (BLE_SERVER_MAX_DATA_LEN - 4))
    {
        ble_comu_response_or_expansion_cmd(cmd_type, (uint8 *)buf, (uint8)len);
    }

    my_stop_timer(MY_TIMER_DC_CTRL_ACK);
    dc_ctrl_ack_ctx_clear();
}

/* 轮询控制结果定时器回调：每 500ms 轮询一次，最多 3 次，超时则推送 FAIL */
static void dc_ctrl_ack_timer_cb(void *param)
{
    uint8 ctrl_result = MY_DC_CTRL_RET_IDLE;
    uint8 err_code = 0;

    (void)param;

    if (!g_dc_ctrl_ack_ctx.valid) // 无待确认控制指令，直接返回
    {
        my_stop_timer(MY_TIMER_DC_CTRL_ACK);
        return;
    }

    if (my_dc_get_last_ctrl_result(&ctrl_result, &err_code) != 0) // 查询控制结果失败，推送 FAIL
    {
        dc_ctrl_ack_send_result(0);
        return;
    }

    if (ctrl_result == MY_DC_CTRL_RET_OK) // 控制成功，推送 OK
    {
        dc_ctrl_ack_send_result(1);
        return;
    }

    if (ctrl_result == MY_DC_CTRL_RET_EXCEPTION || /* 控制失败：重发次数未达上限则重发，否则推送 FAIL */
        ctrl_result == MY_DC_CTRL_RET_TIMEOUT ||
        ctrl_result == MY_DC_CTRL_RET_SEND_FAIL)
    {
        if (g_dc_ctrl_ack_ctx.retry_count < DC_CTRL_ACK_MAX_CHECK)
        {
            g_dc_ctrl_ack_ctx.retry_count++;
            g_dc_ctrl_ack_ctx.check_count = 0; /* 重发后重置轮询计数，给新请求完整 3 次检查机会 */
            if (my_dc_ctrl_switch(g_dc_ctrl_req.sw_id, g_dc_ctrl_req.onoff) != 0)
            {
                my_log_printf(1, "dc ctrl retry dispatch failed. retry=%u", (unsigned)g_dc_ctrl_ack_ctx.retry_count);
            }
            return;
        }
        my_log_printf(1, "dc ctrl fail. result=%d, err=0x%x", ctrl_result, err_code);
        dc_ctrl_ack_send_result(0);
        return;
    }

    g_dc_ctrl_ack_ctx.check_count++; // 轮询次数+1
    if (g_dc_ctrl_ack_ctx.check_count >= DC_CTRL_ACK_MAX_CHECK)
    {
        my_log_printf(1, "dc ctrl ack pending timeout"); // 轮询超时，推送 FAIL
        dc_ctrl_ack_send_result(0);
    }
}

/* 提交开关控制：写入 g_dc_ctrl_req，发 MY_MSG_DC_CTRL_REQ；由常驻 tick 线程调度发送 */
int my_dc_ctrl_switch(my_dc_sw_id_t sw_id, uint8 onoff)
{
    uint8 reg_bit = 0;
    uint16 target_reg = 0;

    if (sw_id > MY_DC_SW_LED)
    {
        return -1;
    }

    /* 有在途请求时，直接返回失败 */
    if (g_dc_ctrl_req.waiting_confirm)
    {
        g_dc_last_ctrl_result = MY_DC_CTRL_RET_SEND_FAIL;
        g_dc_last_ctrl_err = 0;
        return -1;
    }

    /* 开关量寄存器无效时，直接返回失败 */
    /* 只有拿到可信的 0x0001 基线后，才能计算 01 05 的完整寄存器目标值。 */
    if (!g_dc_switch_reg_ready)
    {
        g_dc_last_ctrl_result = MY_DC_CTRL_RET_SEND_FAIL;
        g_dc_last_ctrl_err = 0;
        return -1;
    }

    /* 获取开关对应的寄存器位 */
    reg_bit = dc_sw_to_reg_bit(sw_id);
    if (reg_bit == 0xFF)
    {
        g_dc_last_ctrl_result = MY_DC_CTRL_RET_SEND_FAIL;
        g_dc_last_ctrl_err = 0;
        return -1;
    }

    /* 读取当前 0x0001 寄存器值，并在此基础上计算目标值。 */
    target_reg = dc_get_reg_value(DC_REG_SWITCH_FLAGS, 0);
    if (onoff)
    {
        target_reg |= (uint16)(1u << reg_bit);
    }
    else
    {
        target_reg &= (uint16)(~(1u << reg_bit));
    }

    /* 设置控制请求 */
    g_dc_ctrl_req.valid = 1;
    g_dc_ctrl_req.sw_id = sw_id;
    g_dc_ctrl_req.onoff = onoff ? 1 : 0;
    g_dc_ctrl_req.target_reg_value = target_reg;
    /* 新请求入队时清确认状态 */
    g_dc_ctrl_req.waiting_confirm = 0;
    /* 设置控制结果 */
    g_dc_last_ctrl_result = MY_DC_CTRL_RET_PENDING;
    g_dc_last_ctrl_err = 0;

    my_send_msg(MOD_MAIN, MOD_DC_UART, MY_MSG_DC_CTRL_REQ);
    return 0;
}

/* 提交开关控制并启动 ACK 定时器；结果通过 BLE 推送 RETURN_<cmd>_<state>_OK/FAIL */
int my_dc_ctrl_switch_with_ack(my_dc_sw_id_t sw_id, uint8 onoff, const char *cmd, const char *state)
{
#if MY_DC_MOCK_EN
    /* 模拟模式：不发送 Modbus，直接置成功，定时器首次回调时推送 OK */
    if (sw_id > MY_DC_SW_LED)
    {
        return -1;
    }
    g_dc_last_ctrl_result = MY_DC_CTRL_RET_OK;
    g_dc_last_ctrl_err = 0;
    g_dc_ctrl_ack_ctx.valid = 1;
    g_dc_ctrl_ack_ctx.check_count = 0;
    g_dc_ctrl_ack_ctx.retry_count = 0;
    snprintf(g_dc_ctrl_ack_ctx.cmd, sizeof(g_dc_ctrl_ack_ctx.cmd), "%s", cmd ? cmd : "");
    snprintf(g_dc_ctrl_ack_ctx.state, sizeof(g_dc_ctrl_ack_ctx.state), "%s", state ? state : "");

    if (!my_start_timer(MY_TIMER_DC_CTRL_ACK, DC_CTRL_ACK_CHECK_MS, true, dc_ctrl_ack_timer_cb))
    {
        dc_ctrl_ack_ctx_clear();
        return -1;
    }
    return 0;
#else
    if (my_dc_ctrl_switch(sw_id, onoff) != 0)
    {
        return -1;
    }

    g_dc_ctrl_ack_ctx.valid = 1;
    g_dc_ctrl_ack_ctx.check_count = 0;
    g_dc_ctrl_ack_ctx.retry_count = 0;
    snprintf(g_dc_ctrl_ack_ctx.cmd, sizeof(g_dc_ctrl_ack_ctx.cmd), "%s", cmd ? cmd : "");
    snprintf(g_dc_ctrl_ack_ctx.state, sizeof(g_dc_ctrl_ack_ctx.state), "%s", state ? state : "");

    if (!my_start_timer(MY_TIMER_DC_CTRL_ACK, DC_CTRL_ACK_CHECK_MS, true, dc_ctrl_ack_timer_cb))
    {
        dc_ctrl_ack_ctx_clear();
        return -1;
    }
    return 0;
#endif
}

/* 查询 DC 链路在线状态；连续 3 次超时后为 0 */
int my_dc_is_online(void)
{
#if MY_DC_MOCK_EN
    return 1; /* 模拟模式：始终在线 */
#else
    return g_dc_online ? 1 : 0;
#endif
}

/* 查询最近一次开关控制结果；供 dc_ctrl_ack_timer_cb 轮询 */
int my_dc_get_last_ctrl_result(uint8 *result, uint8 *err_code)
{
    if (result == NULL || err_code == NULL)
    {
        return -1;
    }

    *result = g_dc_last_ctrl_result;
    *err_code = g_dc_last_ctrl_err;
    return 0;
}

void my_dc_uart_task(void *p_arg)
{
    int ret = 0;
    int msg[8] = {0};
    int32 len = 0;
    uint8 *rx_buff = (uint8 *)dma_malloc(MY_DC_UART_RX_TMP_BUF_LEN + 1);

    (void)p_arg;

    my_dc_uart_init();
    my_log_printf(1, "############# my_dc_uart_task is runing ##################");

    while (1)
    {
        ret = os_taskq_pend(NULL, msg, ARRAY_SIZE(msg));
        if (ret != OS_TASKQ)
        {
            continue;
        }

        switch (msg[0])
        {
            case MY_MSG_UART_RECV:
            {
                if (rx_buff == NULL)
                {
                    break;
                }

                memset(rx_buff, 0, MY_DC_UART_RX_TMP_BUF_LEN + 1);
                len = my_dc_uart_read_data(rx_buff, MY_DC_UART_RX_TMP_BUF_LEN);
                if (len > 0)
                {
                    my_dc_proto_feed(rx_buff, (uint32)len);
                }
                break;
            }

            case MY_MSG_DC_POLL_START:
                dc_send_ble_icon_once(1);
                break;

            case MY_MSG_DC_POLL_STOP:
                dc_send_ble_icon_once(0);
                break;

            case MY_MSG_DC_POLL_TICK:
                dc_handle_poll_tick();
                break;

            case MY_MSG_DC_CTRL_REQ:
            {
                // 无在途请求时，才可发新请求
                if (!g_dc_req.active)
                {
                    dc_try_send_ctrl_req(); // 发控制请求
                }
                break;
            }

            default:
                break;
        }
    }

    if (rx_buff != NULL)
    {
        dma_free(rx_buff);
        rx_buff = NULL;
    }
}
