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
 * 【协议文档】DC EMS PPS001 Modbus485 通信协议 V1.5
 * ============================================================================
 *
 * 1. 通讯参数（协议 3.1）：
 *    - RS485，1 起始位，8 数据位，1 停止位，无校验，波特率 9600
 *    - 装置地址固定 0x01
 *    - 每帧不超过 255 字节；正确命令 500ms 内正常响应；错误命令 500ms 内异常响应
 *
 * 2. 数据格式（协议 3.2）：
 *    | 装置地址(1B) | 功能码(1B) | 数据区(NB) | CRC(2B) |
 *    - 数据区 Big Endian，先发高字节
 *
 * 3. 功能码（协议 3.2.2）：
 *    | 01 | 读开关   | 读取一个或多个开关量状态 |
 *    | 03 | 读寄存器 | 读取一个或多个寄存器（模拟量） |
 *    | 05 | 写单线圈 | 写入控制开关，0xFF00=开 0x0000=关 |
 *    | 06 | 写单寄存器| 写入一个寄存器 |
 *    - 异常应答：功能码|0x80，数据区为错误码（1=无效报文 2=地址/长度 3=数值无效 6=装置忙）
 *
 * 4. 寄存器点表（协议 2.1 节选）：
 *    | 0x0001 | 开关量电表(只读) | 0x0004 充电功率 0.1W | 0x0007 电池SOC 0.1% |
 *    | 0x0009 | 剩余可用时间 0.1H | 0x000B 电池温度 0.1℃ | 0x000D 输出总功率 0.1W |
 *    | 0x000E | 逆变器功率 0.1W | 0x000F USB总功率 0.1W | 0x0013 DC5521功率 0.1W |
 *    | 0x0014 | LED功率 0.1W |
 *
 * 5. 开关量点表（协议 2.2，0x0003 读写控制位）：
 *    | Bit0 | 开机 | Bit1 逆变器 | Bit2 USB | Bit3 DC5521 | Bit4 LED |
 *    - 写线圈地址：0x0001=逆变器 0x0002=USB 0x0003=DC5521 0x0004=LED
 *
 * 6. CRC16（协议 3.4）：初值 0xFFFF，多项式 0xA001，最后高低字节交换
 * ============================================================================
 */

/* ========== Modbus 协议参数（协议 3.2.1、3.2.2） ========== */
#define DC_MODBUS_ADDR            0x01 /* 装置地址固定 0x01 */
#define DC_MODBUS_FUNC_READ_COIL  0x01 /* 读开关量 */
#define DC_MODBUS_FUNC_READ_REG   0x03 /* 读寄存器 */
#define DC_MODBUS_FUNC_WRITE_COIL 0x05 /* 写单线圈 */
#define DC_MODBUS_FUNC_WRITE_REG  0x06 /* 写单寄存器 */

/* 写单线圈控制命令（协议 3.3.3）：0xFF00=打开 0x0000=关闭 */
#define DC_MODBUS_CTRL_ON  0xFF00
#define DC_MODBUS_CTRL_OFF 0x0000

/*
 * 业务关注的寄存器地址（协议 2.1 寄存器点表）
 * 实际值=寄存器值/10（系数 0.1），单位见协议
 */
#define DC_REG_STATUS_1        0x0001 /* 开关量电表 0x0001~0x0002，Bit 见 2.2 */
#define DC_REG_CHARGE_POWER    0x0004 /* 充电功率 0.1W */
#define DC_REG_BAT_SOC         0x0007 /* 电池SOC 0.1% */
#define DC_REG_REMAIN_TIME     0x0009 /* 剩余可用时间 0.1H */
#define DC_REG_BAT_TEMP        0x000B /* 电池温度 0.1℃ */
#define DC_REG_TOTAL_POWER     0x000D /* 输出总功率 0.1W */
#define DC_REG_AC_POWER        0x000E /* 逆变器功率 0.1W */
#define DC_REG_USB_TOTAL_POWER 0x000F /* USB总功率 0.1W */
#define DC_REG_DC5521_POWER    0x0013 /* DC5521输出功率 0.1W */
#define DC_REG_LED_POWER       0x0014 /* LED输出功率 0.1W */

/* 缓存容量：寄存器镜像、接收拼包缓存、单帧发送缓存 */
#define DC_REG_CACHE_SIZE 0x0028
#define DC_RX_CACHE_SIZE  128
#define DC_TX_FRAME_MAX   16

/* 从机上报反推用：功能码 + 寄存器区间（协议 2.1），用于 byte_cnt 唯一匹配 */
typedef struct
{
    uint8 func;        /* 功能码 */
    uint16 start_addr; /* 起始地址 */
    uint16 quantity;   /* 寄存器个数 */
} dc_report_profile_t;

/* ========== 寄存器与设备数据 ========== */
static uint16 g_dc_reg_cache[DC_REG_CACHE_SIZE] = {0}; /* Modbus 寄存器镜像 */
static device_data g_dc_dev_data = {0};                /* 业务数据快照 */

/* ========== 发送 DMA 缓冲（UART 驱动需 DMA 可访问内存） ========== */
static uint8 *g_dc_tx_dma_buf = NULL; /* init 时分配，deinit 时释放 */

/*
 * 上报 profile 表（协议 2.1 寄存器点表）：用于从机主动上报时根据 byte_cnt 反推寄存器区间
 * - 0x0001~0x0002: 开关量电表（2 寄存器，byte_cnt=4）
 * - 0x0004~0x000F: 核心实时量（12 寄存器，byte_cnt=24）
 * - 0x0011~0x0016: 分路功率（6 寄存器，byte_cnt=12）
 * - 0x0021~0x0027: 事件计数（7 寄存器，byte_cnt=14）
 */
static dc_report_profile_t g_dc_report_profiles[] = {
    {DC_MODBUS_FUNC_READ_REG, 0x0001, 0x0002},
    {DC_MODBUS_FUNC_READ_REG, 0x0004, 0x000C},
    {DC_MODBUS_FUNC_READ_REG, 0x0011, 0x0006},
    {DC_MODBUS_FUNC_READ_REG, 0x0021, 0x0007},
};

#define DC_LOG_FRAME_EN   1 /* 协议层提交：开启帧打印便于 review */
#define DC_LOG_VERBOSE_EN 1 /* 开启解析/上报日志 */

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
    uint16 reg_sw = dc_get_reg_value(DC_REG_STATUS_1, 0);

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
    if (reg_sw & (1 << 1))
    {
        g_dc_dev_data.sw_status |= (1 << 0); /* 逆变器 */
    }

    if (reg_sw & (1 << 3))
    {
        g_dc_dev_data.sw_status |= (1 << 1); /* DC5521 */
    }

    if (reg_sw & (1 << 2))
    {
        g_dc_dev_data.sw_status |= (1 << 2); /* USB */
    }

    if (reg_sw & (1 << 4))
    {
        g_dc_dev_data.sw_status |= (1 << 3); /* LED */
    }
}

/*
 * 根据帧头估算完整帧长度（协议 3.2.3、3.3.1/3.3.2/3.3.3）
 * 用于粘包/拆包：需至少 2 字节看功能码，读应答需 3 字节取 byte_cnt
 * - 异常应答(func|0x80)：5 字节 [addr|func|err|CRC]
 * - 05/06 写应答：8 字节，与请求相同
 * - 01/03 读应答：5 + byte_cnt，byte_cnt 在 buf[2]
 */
static uint8 dc_modbus_resp_expected_len_any(const uint8 *buf, uint16 len)
{
    if (len < 2)
    {
        return 0; /* 长度不足，无法判断 */
    }

    if ((buf[1] & 0x80) != 0)
    {
        return 5; /* 异常应答：addr|func|err|CRC */
    }

    if (buf[1] == DC_MODBUS_FUNC_WRITE_COIL || buf[1] == DC_MODBUS_FUNC_WRITE_REG)
    {
        return 8; /* 写应答固定 8 字节 */
    }

    if (buf[1] == DC_MODBUS_FUNC_READ_COIL || buf[1] == DC_MODBUS_FUNC_READ_REG)
    {
        if (len < 3)
        {
            return 0; /* 需 byte_cnt 在 buf[2] */
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
    if (func != DC_MODBUS_FUNC_READ_COIL &&
        func != DC_MODBUS_FUNC_READ_REG &&
        func != DC_MODBUS_FUNC_WRITE_COIL &&
        func != DC_MODBUS_FUNC_WRITE_REG)
    {
        return 0; /* 功能码非法 */
    }

    calc_crc = dc_modbus_crc16(frame, frame_len - 2);
    rx_crc = (uint16)((frame[frame_len - 2] << 8) | frame[frame_len - 1]);
    return (calc_crc == rx_crc) ? 1 : 0;
}

/*
 * 根据从机上报的 byte_cnt 反推寄存器区间；仅当唯一匹配时返回 1
 * 避免不同区间长度相同导致误判（如后续协议新增同长度类型需增加区分字段）
 */
static int dc_get_report_profile(uint8 byte_cnt, uint16 *start_addr, uint16 *quantity)
{
    uint8 i = 0;
    uint8 match_idx = 0xFF;
    uint8 match_cnt = 0;

    /* 遍历 profile 表，统计 byte_cnt == quantity*2 的匹配数 */
    for (i = 0; i < ARRAY_SIZE(g_dc_report_profiles); i++)
    {
        if (g_dc_report_profiles[i].func != DC_MODBUS_FUNC_READ_REG)
        {
            continue;
        }
        if (byte_cnt == (uint8)(g_dc_report_profiles[i].quantity * 2))
        {
            match_idx = i;
            match_cnt++;
        }
    }

    // NOTE: 目前因为 byte_cnt 对应 g_dc_report_profiles 只有一种情况
    if (match_cnt != 1 || match_idx == 0xFF)
    {
        return 0; /* 无匹配或多匹配均拒绝 */
    }

    *start_addr = g_dc_report_profiles[match_idx].start_addr;
    *quantity = g_dc_report_profiles[match_idx].quantity;
    return 1;
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
    dc_update_device_data_cache();
}

/*
 * 尝试按从机主动上报解析：功能码 03 + byte_cnt 唯一匹配才做解析，避免误判
 * 返回 1 表示解析成功并已更新 reg_cache、device_data
 */
static int dc_try_handle_report_frame(const uint8 *frame, uint16 frame_len)
{
    uint16 start_addr = 0;
    uint16 quantity = 0;

    (void)frame_len;
    if (frame[1] != DC_MODBUS_FUNC_READ_REG)
    {
        return 0; /* 非 03 帧 */
    }

    if (!dc_get_report_profile(frame[2], &start_addr, &quantity))
    {
        return 0; /* byte_cnt 无唯一匹配 */
    }
    dc_update_read_reg_cache(frame, start_addr);
    DC_LOG_VERBOSE("dc report parsed. start=0x%x, qty=%d", start_addr, quantity);
    return 1;
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
        if (!dc_try_handle_report_frame(rx_cache, expected_len))
        {
            DC_LOG_VERBOSE("dc unknown frame. func=0x%x", rx_cache[1]);
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

    DC_LOG_VERBOSE("dc uart init ok. port=%d", MY_DC_UART_PORT);
}

void my_dc_uart_deinit(void)
{
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

    dc_log_frame("dc rx chunk", data, (uint16)len);
    dc_try_parse_rx_cache(data, len);
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
                    // my_dc_uart_send(rx_buff, (uint32)len); // 临时回环验证：收到什么就回发什么
                    my_dc_proto_feed(rx_buff, (uint32)len);
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

