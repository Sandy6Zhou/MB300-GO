/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : my_led_spi_5050_bt002.c
 * Brief      : bt002灯带驱动模块
 * Version    : V1.0.0
 * Author     : 黄俊凯(huangjunkai@jimiiot.com)
 * Date       : 2026-01-05
************************************************************************************************/
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".my_led_spi_5050_bt002.data.bss")
#pragma data_seg(".my_led_spi_5050_bt002.data")
#pragma const_seg(".my_led_spi_5050_bt002.text.const")
#pragma code_seg(".my_led_spi_5050_bt002.text")
#endif

#include "my_common.h"

/* SPI灯效内部开关：1=启用mode7频谱能量驱动，0=关闭 */
#define MY_LED_SPI_SPECTRUM_EN  1

#if MY_LED_SPI_SPECTRUM_EN
/* mode7 能量→亮灯数；ON_EXTRA 仅用于 fill style 0/1，style2 不加以免静音仍有一圈 */
#define MY_LED_MODE7_ENERGY_DIV         75
#define MY_LED_MODE7_ON_EXTRA           2
#include "effects/spectrum/spectrum_fft.h"
#include "media/framework/include/jlstream.h"
#include "media/framework/include/node_uuid.h"
#endif

/*
 * 5050-BT002灯带采用单线归零码协议，每一个码元都必须有低电平。
 * 每个码元起始为都高电平，高电平时间宽度决定“0”码或者“1”码。
 *
 * “0”码：高电平时间295ns，低电平时间595ns，容差50ns
 * “1”码：高电平时间595ns，低电平时间295ns，容差50ns
 * reset码：低电平时间≥80us
 *
 * spi时钟频率为8MHz，则输出1个bit的脉宽是125ns，优先保证高电平时间
 * 因此输出“1”码需要5个高电平（625ns）+3个低电平（375ns）
 * 输出“0”码需要2个高电平（250ns）+6个低电平（750ns）
**/
#define LED_DATA_BIT_HIGH     0b11111000
#define LED_DATA_BIT_LOW      0b11000000

#define MY_LED_MODE_TICK_MS     10
/* mode7 频谱采样周期（×10ms）。过大时 dB 回落慢，暂停后会「挂亮」更久 */
#define MY_LED_SPEC_SAMPLE_TICKS 2
/* 调试：输出 style2 每帧目标半径（用于判断“下一次应亮几颗”） */
#define MY_LED_MODE7_DEBUG_TARGET_LOG 0
#if MY_LED_MODE7_DEBUG_TARGET_LOG
#define MY_LED_MODE7_DEBUG_STATE_SNAPSHOT_LOG 1 /* 周期状态快照日志 */
#define MY_LED_MODE7_DEBUG_TRANS_LOG  1 /* 状态沿日志 */
#define MY_LED_MODE7_DEBUG_TARGET_DIV 5 /* 周期状态快照日志间隔 */
#else
#define MY_LED_MODE7_DEBUG_STATE_SNAPSHOT_LOG 0
#define MY_LED_MODE7_DEBUG_TRANS_LOG  0
#endif
/** 媒体链路与 tone 提示音链路的 jlstream 频谱节点名（需与配置一致） */
#define MY_LED_SPECTRUM_MEDIA_NODE "Spectrum1"
#define MY_LED_SPECTRUM_TONE_NODE  "Spectrum2"
#ifndef NODE_UUID_SPECTRUM
#define NODE_UUID_SPECTRUM 0xF538
#endif

static hw_spi_dev g_led_spi_dev;

static uint8 g_spi_dma_tx_buff[LED_DATA_MAX_LEN + LED_RESET_CODE_LEN];
static int g_led_mode_timer_id;
static uint8 g_led_mode_pixel_cnt;
static uint8 g_led_mode_idx;
static uint8 g_led_mode7_base_idx = 3; // 默认模式3
static uint8 g_led_mode7_fill_style = 2; /* 0=单边 1=两边往中间 2=三峰向外扩 */
static int g_led_mode_step_cnt;
static int g_led_mode_cnt[3];
static uint8 g_led_mode7_release_on; /* style2 释放态：低能量时统一快衰减，确保三柱都可灭净 */
static uint8 g_led_mode7_release_low_cnt; /* 连续低能量计数：避免一帧回落就误触发全体释放 */
static int g_led_mode7_global_env; /* style2 全局能量包络：播放段防抖，避免轻柔音乐误进释放 */

typedef struct
{
    uint16 auto_gain_q8; /* style2(SPEC1) 自动增益Q8 */
    int input_env;       /* style2(SPEC1) 输入能量包络 */
} my_led_mode7_spec1_energy_state_t;

typedef struct
{
    uint8 prog_cur[3];            /* style2(SPEC1) 三分区当前点亮步数 */
    uint8 prog_target[3];         /* style2(SPEC1) 三分区目标步数 */
    uint8 weak_mode;              /* style2(SPEC1) 0=强路径 1=弱路径 */
    uint8 on_map[25];             /* style2(SPEC1) 当前帧点亮掩码 */
    uint8 low_hold_cnt;           /* style2(SPEC1) 低能挂起计数 */
    uint8 strong_clear_on;        /* style2(SPEC1) 强音快灭阶段 */
    uint8 strong_full_hold_cnt;   /* style2(SPEC1) 强音满亮保持计数 */
    uint8 strong_cycle_on;        /* style2(SPEC1) 强音循环锁 */
#if MY_LED_MODE7_DEBUG_TARGET_LOG
    uint8 trans_prev_valid;       /* 状态沿日志：上一帧是否有效 */
    uint8 trans_prev_rel;         /* 状态沿日志：上一帧 release */
    uint8 trans_prev_weak;        /* 状态沿日志：上一帧 weak */
    uint8 trans_prev_cyc;         /* 状态沿日志：上一帧 cycle */
    uint8 trans_prev_sc;          /* 状态沿日志：上一帧 strong_clear */
#endif
} my_led_mode7_spec1_state_t;

static my_led_mode7_spec1_energy_state_t g_led_mode7_spec1_energy = {256, 0};
static my_led_mode7_spec1_state_t g_led_mode7_spec1;
#if MY_LED_SPI_SPECTRUM_EN
static short g_db_data_old[32];
static short g_led_on_table[32];
static u8 g_led_spec_valid;
static u16 g_led_spec_fail_cnt;
static u8 g_led_spec_hit_logged;
#endif

typedef struct
{
    uint8 mode_idx;
    uint8 div_sec_num;
    uint8 sec_led_num;
    uint8 cnt_freq;
    uint8 rgb_val_max[3];
    uint8 rgb_val_min[3];
    int init_cnt[3];
    int next_cnt_dir[3];
    int next_sec_cnt_phase[3];
    int cnt_unit[3];
    int cnt_max_prd[3];
    int pu_cnt_start[3];
    int pu_cnt_end[3];
    int keep_cnt_start[3];
    int keep_cnt_end[3];
    int pd_cnt_start[3];
    int pd_cnt_end[3];
} my_led_mode_t;

typedef struct
{
    uint8 id;                /* 预设编号：当前固定为1（SPEC1） */
    uint16 global_gain_q8;   /* 全频主触发增益（Q8） */
    uint8 global_trim;       /* 主触发映射偏移 */
    uint8 global_step;       /* 主触发步进 */
    uint8 delta_gain;        /* 正向差分权重 */
    int16 avg_db_subtract;   /* 平均dB稳态扣除 */
    uint8 release_enter;     /* 进入释放态阈值（含） */
    uint8 release_exit;      /* 退出释放态阈值（含） */
    uint8 release_enter_cnt; /* 进入释放态连续帧数 */
    uint8 min_base_active;   /* 轻柔段保底基础半径 */
} my_led_mode7_spec_preset_t;

/* style2(SPEC1) 三分区路径表：强路径用于快速扫到全亮，弱路径用于两侧向核心对称补亮 */
static const uint8 g_led_mode7_zone_len[3] = {10, 5, 10};
static const uint8 g_led_mode7_zone_path_strong[3][10] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9},              /* 左区 1~10 */
    {10, 11, 12, 13, 14, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, /* 中区 11~15 */
    {15, 16, 17, 18, 19, 20, 21, 22, 23, 24},    /* 右区 16~25 */
};
static const uint8 g_led_mode7_zone_path_weak[3][10] = {
    {0, 9, 1, 8, 2, 7, 3, 6, 4, 5},              /* 左区弱能量：两侧向核心收拢 */
    {10, 14, 11, 13, 12, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, /* 中区弱能量：两侧向13收拢 */
    {24, 15, 23, 16, 22, 17, 21, 18, 20, 19},    /* 右区弱能量：两侧向核心收拢 */
};

static uint8 my_clamp_pixel_cnt(int pixel_cnt, uint8 min_val);
static int my_wrap_cnt(int cnt, int prd);
static uint8 my_cnt_to_rgb_val(const my_led_mode_t *mode, int colour, int cnt);
static const my_led_mode_t *my_get_mode_by_idx(uint8 mode_idx);
static const my_led_mode_t *my_get_mode7_base_mode(void);
static int my_get_sec_on_num(const my_led_mode_t *mode, int sec_num);
static u8 my_mode7_should_light(int idx_in_sec, int sec_len, int led_on_num);
#if MY_LED_SPI_SPECTRUM_EN
static void my_mode7_update_style2_core_radius(const my_led_mode_t *mode);
static void my_led_get_spectrum(void);
static void my_mode7_spec1_reset_state(void);
static void my_mode7_style2_apply_zone_map(void);
static void my_mode7_spec1_update_state(const my_led_mode7_spec_preset_t *p, int scaled_global_ref, int scaled_global_input, u8 *release_on);
#endif

/**
 * 灯带模式配置说明：
 * mode_idx: 模式编号
 * div_sec_num: 分段数量
 * sec_led_num: 每段灯数量
 * cnt_freq: 计数频率
 * rgb_val_max: 最大亮度值
 * rgb_val_min: 最小亮度值
 * init_cnt: 初始计数值
 * next_cnt_dir: 下一个计数值的增量方向
 */
static const my_led_mode_t g_led_modes[] = {
    {
        .mode_idx = 0, .div_sec_num = 1, .sec_led_num = 25, .cnt_freq = 1,
        .rgb_val_max = {255, 255, 255}, .rgb_val_min = {0, 0, 0},
        .init_cnt = {0, 30, 60}, .next_cnt_dir = {-10, -10, -10}, .next_sec_cnt_phase = {0, 0, 0},
        .cnt_unit = {1, 1, 1}, .cnt_max_prd = {90, 90, 90},
        .pu_cnt_start = {0, 0, 0}, .pu_cnt_end = {30, 30, 30},
        .keep_cnt_start = {30, 30, 30}, .keep_cnt_end = {30, 30, 30},
        .pd_cnt_start = {30, 30, 30}, .pd_cnt_end = {60, 60, 60},
    },
    {
        .mode_idx = 1, .div_sec_num = 1, .sec_led_num = 25, .cnt_freq = 1,
        .rgb_val_max = {255, 255, 255}, .rgb_val_min = {0, 0, 0},
        .init_cnt = {0, 36, 72}, .next_cnt_dir = {6, 6, 6}, .next_sec_cnt_phase = {0, 0, 0},
        .cnt_unit = {1, 1, 1}, .cnt_max_prd = {108, 108, 108},
        .pu_cnt_start = {0, 0, 0}, .pu_cnt_end = {0, 0, 0},
        .keep_cnt_start = {0, 0, 0}, .keep_cnt_end = {54, 54, 54},
        .pd_cnt_start = {54, 54, 54}, .pd_cnt_end = {54, 54, 54},
    },
    {
        .mode_idx = 2, .div_sec_num = 1, .sec_led_num = 25, .cnt_freq = 1,
        .rgb_val_max = {255, 255, 255}, .rgb_val_min = {0, 0, 0},
        .init_cnt = {0, 30, 60}, .next_cnt_dir = {-5, -5, -5}, .next_sec_cnt_phase = {0, 0, 0},
        .cnt_unit = {1, 1, 1}, .cnt_max_prd = {90, 90, 90},
        .pu_cnt_start = {0, 0, 0}, .pu_cnt_end = {0, 0, 0},
        .keep_cnt_start = {0, 0, 0}, .keep_cnt_end = {30, 30, 30},
        .pd_cnt_start = {30, 30, 30}, .pd_cnt_end = {30, 30, 30},
    },
    {
        .mode_idx = 3, .div_sec_num = 1, .sec_led_num = 25, .cnt_freq = 1,
        .rgb_val_max = {255, 255, 255}, .rgb_val_min = {0, 0, 0},
        .init_cnt = {0, 36, 72}, .next_cnt_dir = {2, 2, 2}, .next_sec_cnt_phase = {0, 0, 0},
        .cnt_unit = {1, 1, 1}, .cnt_max_prd = {108, 108, 108},
        .pu_cnt_start = {0, 0, 0}, .pu_cnt_end = {0, 0, 0},
        .keep_cnt_start = {0, 0, 0}, .keep_cnt_end = {36, 36, 36},
        .pd_cnt_start = {36, 36, 36}, .pd_cnt_end = {36, 36, 36},
    },
    {
        .mode_idx = 4, .div_sec_num = 1, .sec_led_num = 25, .cnt_freq = 1,
        .rgb_val_max = {255, 255, 255}, .rgb_val_min = {0, 0, 0},
        .init_cnt = {0, 36, 72}, .next_cnt_dir = {-12, -12, -12}, .next_sec_cnt_phase = {0, 0, 0},
        .cnt_unit = {1, 1, 1}, .cnt_max_prd = {108, 108, 108},
        .pu_cnt_start = {0, 0, 0}, .pu_cnt_end = {12, 12, 12},
        .keep_cnt_start = {12, 12, 12}, .keep_cnt_end = {24, 24, 24},
        .pd_cnt_start = {24, 24, 24}, .pd_cnt_end = {36, 36, 36},
    },
    {
        .mode_idx = 5, .div_sec_num = 1, .sec_led_num = 25, .cnt_freq = 1,
        .rgb_val_max = {255, 255, 255}, .rgb_val_min = {0, 0, 0},
        .init_cnt = {0, 36, 72}, .next_cnt_dir = {2, 2, 2}, .next_sec_cnt_phase = {0, 0, 0},
        .cnt_unit = {1, 1, 1}, .cnt_max_prd = {108, 108, 108},
        .pu_cnt_start = {0, 0, 0}, .pu_cnt_end = {0, 0, 0},
        .keep_cnt_start = {0, 0, 0}, .keep_cnt_end = {2, 2, 2},
        .pd_cnt_start = {2, 2, 2}, .pd_cnt_end = {2, 2, 2},
    },
    {
        .mode_idx = 6, .div_sec_num = 5, .sec_led_num = 5, .cnt_freq = 1,
        .rgb_val_max = {255, 128, 0}, .rgb_val_min = {200, 32, 0},
        .init_cnt = {0, 0, 0}, .next_cnt_dir = {-18, 18, 0}, .next_sec_cnt_phase = {5, 0, 0},
        .cnt_unit = {1, 0, 0}, .cnt_max_prd = {48, 108, 0},
        .pu_cnt_start = {0, 0, 0}, .pu_cnt_end = {0, 0, 0},
        .keep_cnt_start = {0, 0, 0}, .keep_cnt_end = {24, 1, 0},
        .pd_cnt_start = {24, 1, 0}, .pd_cnt_end = {24, 108, 0},
    },
    {
        .mode_idx = 7, .div_sec_num = 1, .sec_led_num = 25, .cnt_freq = 1,
        .rgb_val_max = {255, 0, 0}, .rgb_val_min = {255, 0, 0},
        .init_cnt = {0, 0, 0}, .next_cnt_dir = {0, 0, 0}, .next_sec_cnt_phase = {0, 0, 0},
        .cnt_unit = {0, 0, 0}, .cnt_max_prd = {0, 0, 0},
        .pu_cnt_start = {0, 0, 0}, .pu_cnt_end = {0, 0, 0},
        .keep_cnt_start = {0, 0, 0}, .keep_cnt_end = {0, 0, 0},
        .pd_cnt_start = {0, 0, 0}, .pd_cnt_end = {0, 0, 0},
    },
};

/************************************************************************
**@brief: SPI 中断回调函数
**@param[in] spi: SPI设备
**@param[in] sta: 中断原因
*************************************************************************/
static void my_led_spi_isr_callback(hw_spi_dev spi, enum hw_spi_isr_status sta)
{
    if (sta == SPI_TX_FINISH)
    {
        // 返回的dma数据长度带了负号
        // my_log_printf(1, "%s(): SPI(%d) DMA tx finish, len:%d", __func__, spi, (-1 * hw_spix_get_isr_len(spi)));
    }
}

/**
 * @brief  初始化 SPI 主机模式
 * @return int 0=成功，<0=失败
 */
static int my_led_spi_init(uint8 spi)
{
    struct spi_platform_data spix_p_data = {
        .port = {
            LED_SPI_CLK_PIN,    // CLK 引脚
            LED_SPI_DO_PIN,     // DO 引脚（连接 LED DIN）
            0xFF,               // DI 引脚（未使用）
            0xFF,               // D2 引脚（未使用）
            0xFF,               // D3 引脚（未使用）
            0xFF,               // CS 引脚（未使用）
        },
        .role = SPI_ROLE_MASTER,        // 主机模式
        .mode = SPI_MODE_BIDIR_1BIT,    // 双工 1bit 模式（仅输出）
        .bit_mode = SPI_FIRST_BIT_MSB,  // 高位先传
        .cpol = 0,                      // 空闲时钟低电平
        .cpha = 0,                      // 第一个时钟沿采样
        .ie_en = 1,                     // 使能中断
        .irq_priority = 3,              // 中断优先级
        .spi_isr_callback = my_led_spi_isr_callback,
        /**
         * 系统启动后4秒，系统时钟频率会被自适应修改到24MHz，导致spi时钟频率
         * 也被改变，因此需要配置固定时钟频率为系统启动默认频率(96MHz)
         */
        .clk = LED_SPI_CLK,
    };

    // 初始化 SPI
    int ret = spi_open(spi, &spix_p_data);
    if (ret < 0)
    {
        my_log_printf(1, "SPI(%d) init failed! ret=%d", spi, ret);
        return ret;
    }

    g_led_spi_dev = spi;
    my_log_printf(1, "SPI(%d) init success, CLK(%d Hz)", spi, spi_get_baud(spi));

    return 0;
}

/************************************************************************
 * @brief  编码单个像素的 RGB 数据（适配单线归零码协议）
 * @param  buff: 输出缓冲区
 * @param  green: 绿色灰度值（0~255）
 * @param  red: 红色灰度值（0~255）
 * @param  blue: 蓝色灰度值（0~255）
 * @note   数据格式：高位在前，G7~G0 → R7~R0 → B7~B0
 *************************************************************************/
static void my_led_encode_pixel(uint8 *buff, uint8 green, uint8 red, uint8 blue)
{
    uint32 pixel_data = ((uint32)green << 16) | ((uint32)red << 8) | blue; // GGRRBB 24bit

    // 逐位编码（单线归零码：高电平时间决定 0/1）
    for (int i = 23; i >= 0; i--)
    {
        if (pixel_data & (1 << i))
        {
            // 1 码：高电平 0.595μs，低电平 0.295μs
            *buff++ = LED_DATA_BIT_HIGH;
        }
        else
        {
            // 0 码：高电平 0.295μs，低电平 0.595μs
            *buff++ = LED_DATA_BIT_LOW;
        }
    }
}

static int my_send_rgb_dma(uint8 pixel_cnt, const uint8 *g, const uint8 *r, const uint8 *b)
{
    int i = 0;
    pixel_cnt = my_clamp_pixel_cnt(pixel_cnt, 0);

    memset(g_spi_dma_tx_buff, 0, sizeof(g_spi_dma_tx_buff));

    for (i = 0; i < pixel_cnt; i++)
    {
        // 编码像素数据
        my_led_encode_pixel(&g_spi_dma_tx_buff[i * LED_PIXEL_BITS], g[i], r[i], b[i]);
    }

    // 发送数据到DMA
    spi_dma_transmit_for_isr(g_led_spi_dev, g_spi_dma_tx_buff, sizeof(g_spi_dma_tx_buff), 0);
    return 0;
}

/**
 * @brief 约束像素数量到有效区间
 */
static uint8 my_clamp_pixel_cnt(int pixel_cnt, uint8 min_val)
{
    if (pixel_cnt < min_val)
    {
        return min_val;
    }
    if (pixel_cnt > LED_MAX_PIXELS)
    {
        return LED_MAX_PIXELS;
    }
    return (uint8)pixel_cnt;
}

/**
 * @brief 将计数值映射到周期范围内
 */
static int my_wrap_cnt(int cnt, int prd)
{
    if (prd == 0)
    {
        return 0;
    }
    while (cnt < 0)
    {
        cnt += prd;
    }
    return cnt % prd;
}

/**
 * @brief 依据模式区间配置将计数值转换为颜色亮度
 */
static uint8 my_cnt_to_rgb_val(const my_led_mode_t *mode, int colour, int cnt)
{
    uint8 rgb_val = mode->rgb_val_min[colour];
    int delta = (int)mode->rgb_val_max[colour] - (int)mode->rgb_val_min[colour];

    if ((mode->pu_cnt_start[colour] <= cnt) && (cnt < mode->pu_cnt_end[colour]))
    {
        int tmp = cnt - mode->pu_cnt_start[colour] + 1;
        int range = mode->pu_cnt_end[colour] - mode->pu_cnt_start[colour];
        if (range > 0)
        {
            rgb_val = mode->rgb_val_min[colour] + (tmp * delta / range);
        }
    }
    else if ((mode->keep_cnt_start[colour] <= cnt) && (cnt < mode->keep_cnt_end[colour]))
    {
        rgb_val = mode->rgb_val_max[colour];
    }
    else if ((mode->pd_cnt_start[colour] <= cnt) && (cnt < mode->pd_cnt_end[colour]))
    {
        int tmp = mode->pd_cnt_end[colour] - cnt - 1;
        int range = mode->pd_cnt_end[colour] - mode->pd_cnt_start[colour];
        if (range > 0)
        {
            rgb_val = mode->rgb_val_min[colour] + (tmp * delta / range);
        }
    }

    return rgb_val;
}

/**
 * @brief 根据模式号查询模式配置
 */
static const my_led_mode_t *my_get_mode_by_idx(uint8 mode_idx)
{
    int i = 0;
    for (i = 0; i < (int)(sizeof(g_led_modes) / sizeof(g_led_modes[0])); i++)
    {
        if (g_led_modes[i].mode_idx == mode_idx)
        {
            return &g_led_modes[i];
        }
    }
    return NULL;
}

/**
 * @brief 获取 mode7 叠加显示时使用的基础模式（限定0~6）
 */
static const my_led_mode_t *my_get_mode7_base_mode(void)
{
    const my_led_mode_t *base_mode = NULL;
    if (g_led_mode7_base_idx > 6)
    {
        g_led_mode7_base_idx = 1;
    }
    base_mode = my_get_mode_by_idx(g_led_mode7_base_idx);
    if (!base_mode)
    {
        base_mode = my_get_mode_by_idx(1);
    }
    return base_mode;
}

/**
 * @brief mode7按频谱能量计算每段亮灯数量，其他mode默认整段点亮
 */
static int my_get_sec_on_num(const my_led_mode_t *mode, int sec_num)
{
    int on_num = mode->sec_led_num;
    if (mode->mode_idx != 7)
    {
        return on_num;
    }
#if MY_LED_SPI_SPECTRUM_EN
    if (!g_led_spec_valid)
    {
        return 0; // 能量无效，不亮灯
    }

    if (g_led_mode7_fill_style == 2)
    {
        /* style2 由三核心独立半径判灯，这里无需再计算整段 on_num */
        return 0;
    }

    if (mode->div_sec_num > 0)
    {
        short sum0 = 0;
        short sum1 = 0;
        int tmp = 32 / mode->div_sec_num;
        int i = 0;
        int energy_div = MY_LED_MODE7_ENERGY_DIV;
        for (i = 0; i < tmp; i++)
        {
            int idx = i + sec_num * tmp;
            if (idx >= 32)
            {
                break;
            }
            sum0 += g_led_on_table[idx];
            sum1 += g_db_data_old[idx];
        }

        sum0 /= 5;
        sum1 /= 5;
        on_num = ((sum1 * mode->sec_led_num / energy_div) + sum0);
        if (on_num < 0)
        {
            on_num = 0;
        }
        if (mode->sec_led_num > 0)
        {
            on_num %= mode->sec_led_num;
        }

        /* style0/1 保留 ON_EXTRA，避免能量较小时过于稀疏 */
        if (g_led_mode7_fill_style != 2)
        {
            on_num += MY_LED_MODE7_ON_EXTRA;
        }
        if (on_num > mode->sec_led_num)
        {
            on_num = mode->sec_led_num;
        }
    }
#endif
    return on_num;
}

/**
 * @brief mode7 判断当前像素在指定风格下是否点亮
 */
static u8 my_mode7_should_light(int idx_in_sec, int sec_len, int led_on_num)
{
    /* 安全兜底：无效段长时按25灯处理 */
    if (sec_len <= 0)
    {
        return 0;
    }
    if (led_on_num < 0)
    {
        led_on_num = 0;
    }
    if (led_on_num > sec_len)
    {
        led_on_num = sec_len;
    }
    if (g_led_mode7_fill_style == 1)
    {
        int left_num = (led_on_num + 1) / 2;
        int right_num = led_on_num / 2;
        return (idx_in_sec < left_num) || (idx_in_sec >= (sec_len - right_num));
    }

    if (g_led_mode7_fill_style == 2)
    {
#if MY_LED_SPI_SPECTRUM_EN
        if (sec_len == 25)
        {
            return g_led_mode7_spec1.on_map[idx_in_sec] ? 1 : 0;
        }
        return (idx_in_sec < led_on_num);
#endif
    }
    return (idx_in_sec < led_on_num);
}

#if MY_LED_SPI_SPECTRUM_EN
#define MY_LED_MODE7_SPEC_EMPTY_BAND  (-30000)

/**
 * @brief 指定 bin 段能量（未做 Q8）：max(0,均值dB-subtract)+正向delta*gain，突出动态、减弱「整条常亮」
 */
static int my_mode7_spec_avg_energy_raw(int b_start, int b_end, const my_led_mode7_spec_preset_t *p)
{
    int i;
    int cnt = 0;
    int sum_db = 0;
    int sum_delta = 0;
    int avg_db;
    int avg_eff;
    int avg_delta;
    int delta_pos;

    for (i = b_start; i <= b_end; i++)
    {
        if ((i < 0) || (i >= 32))
        {
            continue;
        }
        sum_db += g_db_data_old[i];
        sum_delta += g_led_on_table[i];
        cnt++;
    }

    if (cnt <= 0)
    {
        return MY_LED_MODE7_SPEC_EMPTY_BAND;
    }

    avg_db = sum_db / cnt;
    avg_eff = avg_db - (int)p->avg_db_subtract;
    if (avg_eff < 0)
    {
        avg_eff = 0;
    }
    avg_delta = sum_delta / cnt;
    delta_pos = avg_delta > 0 ? avg_delta : 0;
    return avg_eff + (delta_pos * p->delta_gain);
}

/**
 * @brief 原始能量按 Q8 增益缩放，负值归零
 */
static int my_mode7_spec_scale_energy_q8(int energy_raw, uint16 gain_q8)
{
    int scaled;

    if (energy_raw <= MY_LED_MODE7_SPEC_EMPTY_BAND + 1)
    {
        return 0;
    }
    scaled = (energy_raw * (int)gain_q8 + 128) >> 8;
    if (scaled < 0)
    {
        scaled = 0;
    }
    return scaled;
}

/**
 * @brief 全局主触发映射到基础半径
 */
static int my_mode7_spec_global_to_base(int scaled_global, const my_led_mode7_spec_preset_t *p)
{
    int ex = scaled_global - (int)p->global_trim;
    int step = p->global_step > 0 ? p->global_step : 1;
    int base;

    if (ex <= 0)
    {
        return 0;
    }
    base = (ex + step - 1) / step;
    /* 轻柔音乐保护：高于 release_enter 的有效能量至少点亮 1 档 */
    if ((scaled_global > (int)p->release_enter) && (base < (int)p->min_base_active))
    {
        base = p->min_base_active;
    }
    return base;
}

/**
 * @brief 分频相对全局的偏置半径（仅微调，不决定亮灭）
 */
enum
{
    MY_LED_MODE7_SPEC1_REASON_NONE = 0,
    MY_LED_MODE7_SPEC1_REASON_REL_EXIT_BY_INPUT = 1,
    MY_LED_MODE7_SPEC1_REASON_HOLD_WEAK = 2,
    MY_LED_MODE7_SPEC1_REASON_HOLD_TO_ZERO = 3,
    MY_LED_MODE7_SPEC1_REASON_WEAK_BRANCH = 4,
    MY_LED_MODE7_SPEC1_REASON_ENTER_STRONG = 5,
    MY_LED_MODE7_SPEC1_REASON_STRONG_TO_CLEAR = 6,
    MY_LED_MODE7_SPEC1_REASON_STRONG_CLEAR_DONE = 7,
    MY_LED_MODE7_SPEC1_REASON_WEAK_FLOOR = 8,
};

static void my_mode7_spec1_reset_state(void)
{
    g_led_mode7_spec1_energy.auto_gain_q8 = 256;
    g_led_mode7_spec1_energy.input_env = 0;
    memset(&g_led_mode7_spec1, 0, sizeof(g_led_mode7_spec1));
}

#if MY_LED_MODE7_DEBUG_TRANS_LOG
static void my_mode7_spec1_log_transition(const my_led_mode7_spec_preset_t *p, u8 release_on, int sg, int sgin, uint8 reason)
{
    static const char *kReasonText[] = {
        "none",
        "rel_exit_by_input",
        "hold_weak",
        "hold_to_zero",
        "weak_branch",
        "enter_strong",
        "strong_to_clear",
        "strong_clear_done",
        "weak_floor",
    };
    my_led_mode7_spec1_state_t *s = &g_led_mode7_spec1;
    const char *reason_text = (reason < (uint8)(sizeof(kReasonText) / sizeof(kReasonText[0]))) ? kReasonText[reason] : "unknown";
    u8 changed = 0;
    /* 首帧只记录基线状态，不输出跳变日志 */
    if (!s->trans_prev_valid)
    {
        s->trans_prev_valid = 1;
        s->trans_prev_rel = release_on;
        s->trans_prev_weak = s->weak_mode;
        s->trans_prev_cyc = s->strong_cycle_on;
        s->trans_prev_sc = s->strong_clear_on;
        return;
    }
    /* 与上一帧对比关键状态位，判断本帧是否发生状态切换 */
    if ((s->trans_prev_rel != release_on) ||
        (s->trans_prev_weak != s->weak_mode) ||
        (s->trans_prev_cyc != s->strong_cycle_on) ||
        (s->trans_prev_sc != s->strong_clear_on))
    {
        changed = 1;
    }
    /* 仅在状态切换或存在有效原因码时输出状态沿日志，避免无意义刷屏 */
    if (changed || (reason != MY_LED_MODE7_SPEC1_REASON_NONE))
    {
        my_log_printf(1, "m7 s2 tr p:%d rs:%d(%s) rel:%d sg:%d sgin:%d weak:%d cyc:%d sc:%d hold:%d",
                      p->id, reason, reason_text, release_on, sg, sgin, s->weak_mode,
                      s->strong_cycle_on, s->strong_clear_on, s->low_hold_cnt);
    }
    /* 更新上一帧快照，供下一帧比较 */
    s->trans_prev_rel = release_on;
    s->trans_prev_weak = s->weak_mode;
    s->trans_prev_cyc = s->strong_cycle_on;
    s->trans_prev_sc = s->strong_clear_on;
}
#endif

static void my_mode7_spec1_update_state(const my_led_mode7_spec_preset_t *p, int scaled_global_ref, int scaled_global_input, u8 *release_on)
{
    my_led_mode7_spec1_state_t *s = &g_led_mode7_spec1;
    my_led_mode7_spec1_energy_state_t *u = &g_led_mode7_spec1_energy;
    int z;
    int weak_enter = 1; /* 特别弱判定：仅在极低能量时走弱路径 */
    int strong_enter = 12; /* strong threshold tuned down for mid-level audio */
    int rise_step = 1;  /* 10 灯区约 100ms 扫满 */
    int fall_step = (*release_on) ? 2 : 1; /* 灭灯更快：按原路径反向每帧回退 2 步 */
    int sg = scaled_global_ref;
    int sgin = scaled_global_input;
    u8 all_full = 1;
    u8 all_zero = 1;
    uint8 trans_reason = MY_LED_MODE7_SPEC1_REASON_NONE;

    /* 如果已有输入能量，优先退出 release，避免“有声但全灭” */
    if (((sgin > 0) || (u->input_env > 0)) && (*release_on))
    {
        /* 有原始能量输入时优先保持活动，减少“小声偶发漏触发” */
        *release_on = 0;
        g_led_mode7_release_on = 0;
        g_led_mode7_release_low_cnt = 0;
        trans_reason = MY_LED_MODE7_SPEC1_REASON_REL_EXIT_BY_INPUT;
    }

    /* release态或主能量掉零：优先走弱保持/保底/灭灯分支 */
    if ((*release_on) || (sg <= 0))
    {
        int release_cnt_th = (p->release_enter_cnt > 0) ? p->release_enter_cnt : 1;
        int weak_activity = ((sgin > 0) || (u->input_env > 0));
        int weak_floor_ok = (!(*release_on) && g_led_spec_valid && (g_led_mode7_release_low_cnt < release_cnt_th));
        s->strong_clear_on = 0;
        s->strong_full_hold_cnt = 0;
        s->strong_cycle_on = 0;
        /* 频谱有效且仍有活动迹象时，进入弱保持窗口，避免瞬时断续闪灭 */
        if ((g_led_spec_valid && (weak_activity || weak_floor_ok)) && (s->low_hold_cnt < 30))
        {
            /* brief hold on zero dips using raw input + input envelope */
            s->low_hold_cnt++;
            s->weak_mode = 1;
            /* 弱保持初期：给稍高目标，保证“有感知的亮” */
            if (weak_activity && (s->low_hold_cnt <= 10))
            {
                s->prog_target[0] = 3;
                s->prog_target[1] = 2;
                s->prog_target[2] = 3;
            }
            /* 弱保持后期：降低目标，保留轻微动态 */
            else if (weak_activity)
            {
                /* 最弱保底层：小声/弱拍下仍有轻微灯效，不至于完全无反应 */
                s->prog_target[0] = 2;
                s->prog_target[1] = 1;
                s->prog_target[2] = 2;
            }
            /* 无活动但仍在容忍窗口：给最低可见保底 */
            else
            {
                /* 低声连续零帧时，至少保留一档可见反馈 */
                s->prog_target[0] = 1;
                s->prog_target[1] = 1;
                s->prog_target[2] = 1;
                trans_reason = MY_LED_MODE7_SPEC1_REASON_WEAK_FLOOR;
            }

            /* 如果无有效原因码，则默认为弱保持 */
            if (trans_reason == MY_LED_MODE7_SPEC1_REASON_NONE)
            {
                trans_reason = MY_LED_MODE7_SPEC1_REASON_HOLD_WEAK;
            }
        }
        /* 条件不满足则转全灭目标，准备按路径回落 */
        else
        {
            /* 进入灭灯时保持当前路径类型，保证按原样反向灭下去 */
            s->prog_target[0] = 0;
            s->prog_target[1] = 0;
            s->prog_target[2] = 0;
            s->low_hold_cnt = 0;
            trans_reason = MY_LED_MODE7_SPEC1_REASON_HOLD_TO_ZERO;
        }
    }
    /* 非release且能量很低：走弱路径，不追求全亮 */
    else if (sg <= weak_enter)
    {
        /* 弱能量：保留对称补亮，不强求全亮 */
        s->weak_mode = 1;
        s->prog_target[0] = (uint8)(3 + sg * 2); /* 3..5 */
        s->prog_target[1] = (uint8)(2 + sg);     /* 2..3 */
        s->prog_target[2] = (uint8)(3 + sg * 2); /* 3..5 */
        s->low_hold_cnt = 0;
        s->strong_clear_on = 0;
        s->strong_full_hold_cnt = 0;
        s->strong_cycle_on = 0;
        trans_reason = MY_LED_MODE7_SPEC1_REASON_WEAK_BRANCH;
    }
    /* 常规/强音分支：中音分级 + 强音循环（扫满->快灭） */
    else
    {
        /*
         * 非特别弱：中音分级；强音执行“顺序扫满 -> 快速全灭”循环。
         */
        s->weak_mode = 0;
        s->low_hold_cnt = 0;

        /* 先判断当前是否“已全满/已全灭”，用于循环阶段切换 */
        for (z = 0; z < 3; z++)
        {
            uint8 lim = g_led_mode7_zone_len[z];
            if (s->prog_cur[z] < lim)
            {
                all_full = 0;
            }
            if (s->prog_cur[z] > 0)
            {
                all_zero = 0;
            }
        }

        /* 达到强音阈值且尚未进入循环：启动强音循环 */
        if ((sg >= strong_enter) && !s->strong_cycle_on)
        {
            s->strong_cycle_on = 1;
            s->strong_clear_on = 0;
            s->strong_full_hold_cnt = 0;
            trans_reason = MY_LED_MODE7_SPEC1_REASON_ENTER_STRONG;
        }

        /* 强音循环内部：扫满阶段 与 快灭阶段 */
        if (s->strong_cycle_on)
        {
            /* 快灭阶段：目标直接归零并加大回落步长 */
            if (s->strong_clear_on)
            {
                /* 强音快灭阶段：按原路径快速回退到全灭 */
                s->prog_target[0] = 0;
                s->prog_target[1] = 0;
                s->prog_target[2] = 0;
                fall_step = 3;
                if (all_zero)
                {
                    s->strong_clear_on = 0;
                    s->strong_full_hold_cnt = 0;
                    s->strong_cycle_on = 0;
                    trans_reason = MY_LED_MODE7_SPEC1_REASON_STRONG_CLEAR_DONE;
                }
            }
            /* 扫满阶段：目标拉满，满亮保持后切到快灭 */
            else
            {
                s->prog_target[0] = g_led_mode7_zone_len[0];
                s->prog_target[1] = g_led_mode7_zone_len[1];
                s->prog_target[2] = g_led_mode7_zone_len[2];
                /* 仅在三区都满亮时累计保持计数 */
                if (all_full)
                {
                    if (s->strong_full_hold_cnt < 255)
                    {
                        s->strong_full_hold_cnt++;
                    }
                    if (s->strong_full_hold_cnt >= 3)
                    {
                        s->strong_clear_on = 1;
                        s->strong_full_hold_cnt = 0;
                        trans_reason = MY_LED_MODE7_SPEC1_REASON_STRONG_TO_CLEAR;
                    }
                }
                else
                {
                    s->strong_full_hold_cnt = 0;
                }
            }
        }
        /* 中音分级1：较低中音 */
        else if (sg <= 3)
        {
            s->strong_clear_on = 0;
            s->strong_full_hold_cnt = 0;
            s->strong_cycle_on = 0;
            s->prog_target[0] = 6;
            s->prog_target[1] = 3;
            s->prog_target[2] = 6;
        }
        /* 中音分级2：中低音 */
        else if (sg <= 6)
        {
            s->strong_clear_on = 0;
            s->strong_full_hold_cnt = 0;
            s->strong_cycle_on = 0;
            s->prog_target[0] = 7;
            s->prog_target[1] = 3;
            s->prog_target[2] = 7;
        }
        /* 中音分级3：中高音 */
        else if (sg <= 10)
        {
            s->strong_clear_on = 0;
            s->strong_full_hold_cnt = 0;
            s->strong_cycle_on = 0;
            s->prog_target[0] = 8;
            s->prog_target[1] = 4;
            s->prog_target[2] = 8;
        }
        /* 非循环且高于中音区间：直接给满目标 */
        else
        {
            s->strong_clear_on = 0;
            s->strong_full_hold_cnt = 0;
            s->strong_cycle_on = 0;
            s->prog_target[0] = g_led_mode7_zone_len[0];
            s->prog_target[1] = g_led_mode7_zone_len[1];
            s->prog_target[2] = g_led_mode7_zone_len[2];
        }
    }

    /* 将每区当前进度按 rise/fall 步长逼近目标进度 */
    for (z = 0; z < 3; z++)
    {
        uint8 lim = g_led_mode7_zone_len[z];
        uint8 cur = s->prog_cur[z];
        uint8 trg = s->prog_target[z];
        if (trg > lim)
        {
            trg = lim;
        }
        s->prog_target[z] = trg;

        if (cur < trg)
        {
            int up = cur + rise_step;
            s->prog_cur[z] = (uint8)(up > trg ? trg : up);
        }
        else if (cur > trg)
        {
            int dn = (int)cur - fall_step;
            s->prog_cur[z] = (uint8)(dn < (int)trg ? trg : dn);
        }
    }

#if MY_LED_MODE7_DEBUG_TRANS_LOG
    my_mode7_spec1_log_transition(p, *release_on, sg, sgin, trans_reason);
#endif
}

/**
 * @brief style2(SPEC1) 按当前进度写入 25 灯点亮掩码
 */
static void my_mode7_style2_apply_zone_map(void)
{
    my_led_mode7_spec1_state_t *state = &g_led_mode7_spec1;
    const uint8 (*path)[10] = state->weak_mode ? g_led_mode7_zone_path_weak : g_led_mode7_zone_path_strong;
    int z;
    int step;

    memset(g_led_mode7_spec1.on_map, 0, sizeof(g_led_mode7_spec1.on_map));
    for (z = 0; z < 3; z++)
    {
        int lim = g_led_mode7_zone_len[z];
        int cur = state->prog_cur[z];
        if (cur > lim)
        {
            cur = lim;
        }
        for (step = 0; step < cur; step++)
        {
            uint8 idx = path[z][step];
            if (idx < 25)
            {
                g_led_mode7_spec1.on_map[idx] = 1;
            }
        }
    }
}

/**
 * @brief mode7 style2: 更新SPEC1三分区状态（25灯）
 */
static void my_mode7_update_style2_core_radius(const my_led_mode_t *mode)
{
    my_led_mode7_spec1_energy_state_t *u = &g_led_mode7_spec1_energy;
    my_led_mode7_spec1_state_t *s = &g_led_mode7_spec1;
    static const my_led_mode7_spec_preset_t kSpec1Preset = {
        .id = 1,
        .global_gain_q8 = 460,
        .global_trim = 1,
        .global_step = 2,
        .delta_gain = 4,
        .avg_db_subtract = 6,
        .release_enter = 5,
        .release_exit = 13,
        .release_enter_cnt = 18,
        .min_base_active = 1,
    };
    const my_led_mode7_spec_preset_t *p = &kSpec1Preset;
    int raw_global;
    int scaled_global;
    int scaled_global_ref;
    int base_radius;
    u8 release_on;
    int scaled_global_input;
    int agc_target = 10;
    int agc_low_band = 3;
    int agc_high_band = 4;
    int agc_gain_min = 192;
    int agc_gain_max = 512;
    uint16 agc_gain_q8;
    int sec_len = mode ? mode->sec_led_num : 0;

    if (sec_len <= 0)
    {
        sec_len = 25;
    }
    /* 频谱无效时直接置入 release，避免残亮 */
    if (!g_led_spec_valid)
    {
        g_led_mode7_release_on = 1;
    }

    raw_global = my_mode7_spec_avg_energy_raw(0, 31, p);
    scaled_global_input = my_mode7_spec_scale_energy_q8(raw_global, p->global_gain_q8);
    scaled_global = scaled_global_input;
    /* 输入包络快起慢落：抑制瞬时抖动 */
    if (scaled_global_input >= u->input_env)
    {
        /* 输入包络：快起慢落，避免 AGC 对瞬时峰值过度反应 */
        u->input_env = (u->input_env + scaled_global_input * 3 + 3) / 4;
    }
    else
    {
        int diff_env = u->input_env - scaled_global_input;
        int drop_env = (diff_env + 5) / 6;
        if (drop_env < 1)
        {
            drop_env = 1;
        }
        u->input_env -= drop_env;
    }

    if (u->input_env < 0)
    {
        u->input_env = 0;
    }

    /* 自动增益：慢速、限幅，避免在强/弱段来回抽动 */
    /* AGC：输入偏低时缓慢抬增益 */
    if (u->input_env < (agc_target - agc_low_band))
    {
        if (u->auto_gain_q8 < agc_gain_max)
        {
            u->auto_gain_q8 += 2;
            if (u->auto_gain_q8 > agc_gain_max)
            {
                u->auto_gain_q8 = (uint16)agc_gain_max;
            }
        }
    }
    /* AGC：输入偏高时缓慢降增益 */
    else if (u->input_env > (agc_target + agc_high_band))
    {
        if (u->auto_gain_q8 > agc_gain_min)
        {
            u->auto_gain_q8 -= 3;
            if (u->auto_gain_q8 < agc_gain_min)
            {
                u->auto_gain_q8 = (uint16)agc_gain_min;
            }
        }
    }

    agc_gain_q8 = u->auto_gain_q8;
    scaled_global = (scaled_global_input * (int)agc_gain_q8 + 128) >> 8;
    /* 高能压缩：减轻长时间顶亮 */
    if (scaled_global > 18)
    {
        /* 高能压缩：避免长时间大半径常亮 */
        scaled_global = 18 + ((scaled_global - 18) / 3);
    }
    /* 全局包络快起慢落 */
    if (scaled_global >= g_led_mode7_global_env)
    {
        /* 快起：避免节奏上冲迟滞 */
        g_led_mode7_global_env = (g_led_mode7_global_env + scaled_global * 3 + 3) / 4;
    }
    else
    {
        int diff = g_led_mode7_global_env - scaled_global;
        int drop = (diff + 3) / 4;
        if (drop < 1)
        {
            drop = 1;
        }
        /* 慢落但保证收敛：每次至少下降1，避免卡在固定值(如 sg=6 常亮) */
        g_led_mode7_global_env -= drop;
        if (g_led_mode7_global_env < scaled_global)
        {
            g_led_mode7_global_env = scaled_global;
        }
    }
    if (g_led_mode7_global_env < 0)
    {
        g_led_mode7_global_env = 0;
    }
    scaled_global_ref = g_led_mode7_global_env;

    /* release回差控制：进入/退出阈值分离 */
    if (g_led_mode7_release_on)
    {
        if (scaled_global_ref >= (int)p->release_exit)
        {
            g_led_mode7_release_on = 0;
            g_led_mode7_release_low_cnt = 0;
        }
    }
    else
    {
        if (scaled_global_ref <= (int)p->release_enter)
        {
            if (g_led_mode7_release_low_cnt < 255)
            {
                g_led_mode7_release_low_cnt++;
            }
            if (g_led_mode7_release_low_cnt >= (p->release_enter_cnt > 0 ? p->release_enter_cnt : 1))
            {
                g_led_mode7_release_on = 1;
                g_led_mode7_release_low_cnt = 0;
            }
        }
        else
        {
            g_led_mode7_release_low_cnt = 0;
        }
    }
    release_on = g_led_mode7_release_on;

    /* 断流时强制清空参考能量，避免包络拖尾 */
    if (release_on && !g_led_spec_valid)
    {
        /* 频谱断流时保留软释放，不做硬清零 */
        scaled_global_ref = 0;
        g_led_mode7_global_env = 0;
    }

    base_radius = my_mode7_spec_global_to_base(scaled_global_ref, p);
    if (base_radius < 0)
    {
        base_radius = 0;
    }
    /* 若已有有效主触发，立即退出release */
    if (release_on && (base_radius > 0))
    {
        /* 有有效主触发时必须退出释放态，否则会出现 trg>0 但 cur 一直为0 */
        release_on = 0;
        g_led_mode7_release_on = 0;
        g_led_mode7_release_low_cnt = 0;
    }

    /* 仅在标准25灯布局走SPEC1分区状态机 */
    if (sec_len == 25)
    {
        my_mode7_spec1_update_state(p, scaled_global_ref, scaled_global_input, &release_on);

        my_mode7_style2_apply_zone_map();

#if MY_LED_MODE7_DEBUG_STATE_SNAPSHOT_LOG
        if ((g_led_mode_step_cnt % MY_LED_MODE7_DEBUG_TARGET_DIV) == 0)
        {
            my_log_printf(1, "m7 s2 p:%d rel:%d sv:%d sg:%d sgin:%d in:%d ag:%d weak:%d hold:%d cyc:%d sc:%d sh:%d ztrg[%d,%d,%d] zcur[%d,%d,%d]",
                          p->id, release_on, g_led_spec_valid, scaled_global_ref, scaled_global_input,
                          u->input_env, (int)u->auto_gain_q8, s->weak_mode,
                          s->low_hold_cnt, s->strong_cycle_on,
                          s->strong_clear_on, s->strong_full_hold_cnt,
                          s->prog_target[0], s->prog_target[1], s->prog_target[2],
                          s->prog_cur[0], s->prog_cur[1], s->prog_cur[2]);
        }
#endif
        return;
    }
}

/**
 * @brief 获取频谱能量并更新 mode7 使用的能量表
 * @note 同时读媒体链与 tone 链频谱节点；同一频点取较大 dB，任一路可读即认为有效
 */
static void my_led_get_spectrum(void)
{
    struct spectrum_parm cfg_media = {0};
    struct spectrum_parm cfg_tone = {0};
    int ret_media = jlstream_get_node_param(NODE_UUID_SPECTRUM, MY_LED_SPECTRUM_MEDIA_NODE, &cfg_media, sizeof(cfg_media));
    int ret_tone = jlstream_get_node_param(NODE_UUID_SPECTRUM, MY_LED_SPECTRUM_TONE_NODE, &cfg_tone, sizeof(cfg_tone));
    u8 ok_media = (ret_media == sizeof(cfg_media)) && cfg_media.db_data;
    u8 ok_tone = (ret_tone == sizeof(cfg_tone)) && cfg_tone.db_data;
    u8 got_data = ok_media || ok_tone;
    int j = 0;

    if (got_data)
    {
        for (j = 0; j < 32; j++)
        {
            u8 have_media = ok_media && (j < (int)cfg_media.db_num);
            u8 have_tone = ok_tone && (j < (int)cfg_tone.db_num);
            short merged;

            if (!have_media && !have_tone)
            {
                continue;
            }
            if (have_media && have_tone)
            {
                merged = cfg_media.db_data[j] > cfg_tone.db_data[j] ? cfg_media.db_data[j] : cfg_tone.db_data[j];
            }
            else if (have_media)
            {
                merged = cfg_media.db_data[j];
            }
            else
            {
                merged = cfg_tone.db_data[j];
            }
            g_led_on_table[j] = merged - g_db_data_old[j];
            g_db_data_old[j] = merged;
        }
    }

    g_led_spec_valid = got_data;
    if (!got_data)
    {
        g_led_spec_fail_cnt++;
        if ((g_led_spec_fail_cnt % 20) == 0)
        {
           // my_log_printf(1, "mode7 spectrum fail(%u) media_ret:%d tone_ret:%d", g_led_spec_fail_cnt, ret_media, ret_tone);
        }
    }
    else
    {
        if (!g_led_spec_hit_logged)
        {
            my_log_printf(1, "mode7 spectrum ok uuid:0x%x media:%s tone:%s", NODE_UUID_SPECTRUM,
                          MY_LED_SPECTRUM_MEDIA_NODE, MY_LED_SPECTRUM_TONE_NODE);
            g_led_spec_hit_logged = 1;
        }
        g_led_spec_fail_cnt = 0;
    }
}
#endif

/**
 * @brief 模式扫描定时回调：按当前模式生成一帧并发送到SPI
 */
static void my_led_mode_scan(void *priv)
{
    uint8 g_arr[LED_MAX_PIXELS] = {0};
    uint8 r_arr[LED_MAX_PIXELS] = {0};
    uint8 b_arr[LED_MAX_PIXELS] = {0};
    int i = 0;
    const my_led_mode_t *mode = my_get_mode_by_idx(g_led_mode_idx);
    const my_led_mode_t *render_mode = mode;

    (void)priv;
    if (!mode)
    {
        return;
    }
    if (mode->mode_idx == 7)
    {
        render_mode = my_get_mode7_base_mode();
        if (!render_mode)
        {
            return;
        }
    }

    if (render_mode->cnt_freq && ((g_led_mode_step_cnt % render_mode->cnt_freq) == 0))
    {
        for (i = 0; i < 3; i++)
        {
            g_led_mode_cnt[i] = my_wrap_cnt(g_led_mode_cnt[i] + render_mode->cnt_unit[i], render_mode->cnt_max_prd[i]);
        }
    }

#if MY_LED_SPI_SPECTRUM_EN
    if ((mode->mode_idx == 7) && ((g_led_mode_step_cnt % MY_LED_SPEC_SAMPLE_TICKS) == 0))
    {
        my_led_get_spectrum();
    }

    if ((mode->mode_idx == 7) && (g_led_mode7_fill_style == 2))
    {
        my_mode7_update_style2_core_radius(mode);
    }
#endif

    for (i = 0; i < g_led_mode_pixel_cnt; i++)
    {
        int sec_led_num = (mode->sec_led_num > 0) ? mode->sec_led_num : g_led_mode_pixel_cnt;
        int sec = i / sec_led_num;
        int sec_start = sec * sec_led_num;
        int sec_len = sec_led_num;
        int idx_in_sec = i - sec_start;
        int led_on_num = my_get_sec_on_num(mode, sec);
        u8 led_is_on = 0;
        if ((sec_start + sec_len) > g_led_mode_pixel_cnt)
        {
            sec_len = g_led_mode_pixel_cnt - sec_start;
        }
        int cnt_r = my_wrap_cnt(g_led_mode_cnt[0] + (sec * render_mode->next_sec_cnt_phase[0] + idx_in_sec) * render_mode->next_cnt_dir[0], render_mode->cnt_max_prd[0]);
        int cnt_g = my_wrap_cnt(g_led_mode_cnt[1] + (sec * render_mode->next_sec_cnt_phase[1] + idx_in_sec) * render_mode->next_cnt_dir[1], render_mode->cnt_max_prd[1]);
        int cnt_b = my_wrap_cnt(g_led_mode_cnt[2] + (sec * render_mode->next_sec_cnt_phase[2] + idx_in_sec) * render_mode->next_cnt_dir[2], render_mode->cnt_max_prd[2]);
        r_arr[i] = my_cnt_to_rgb_val(render_mode, 0, cnt_r);
        g_arr[i] = my_cnt_to_rgb_val(render_mode, 1, cnt_g);
        b_arr[i] = my_cnt_to_rgb_val(render_mode, 2, cnt_b);
        if (mode->mode_idx == 7)
        {
            led_is_on = my_mode7_should_light(idx_in_sec, sec_len, led_on_num);
        }
        else
        {
            led_is_on = (idx_in_sec < led_on_num);
        }
        if (!led_is_on)
        {
            r_arr[i] = 0;
            g_arr[i] = 0;
            b_arr[i] = 0;
        }
    }

    my_send_rgb_dma(g_led_mode_pixel_cnt, g_arr, r_arr, b_arr);
    g_led_mode_step_cnt++;
}

/************************************************************************
**@brief: 设置指定数量 LED 为指定颜色
**@param[in] red:       红色灰度值（0~255）
**@param[in] green:     绿色灰度值（0~255）
**@param[in] blue:      蓝色灰度值（0~255）
**@param[in] pixel_cnt: 像素点数量（0~15）
*************************************************************************/
void my_led_set_rgb(uint8 red, uint8 green, uint8 blue, uint8 pixel_cnt)
{
    int i = 0;

    pixel_cnt = my_clamp_pixel_cnt(pixel_cnt, 0);

    // 生成颜色数组
    uint8 g_arr[LED_MAX_PIXELS] = {0};
    uint8 r_arr[LED_MAX_PIXELS] = {0};
    uint8 b_arr[LED_MAX_PIXELS] = {0};

    for (i = 0; i < pixel_cnt; i++)
    {
        g_arr[i] = green;
        r_arr[i] = red;
        b_arr[i] = blue;
    }

    // 编码数据并发送
    my_send_rgb_dma(pixel_cnt, g_arr, r_arr, b_arr);
}

/************************************************************************
**@brief  LED 驱动初始化入口
**@return int 0=成功，<0=失败
 *************************************************************************/
int my_led_5050_bt002_init(uint8 spi)
{
    // 初始化 SPI
    int ret = my_led_spi_init(spi);
    if (ret != 0)
    {
        return ret;
    }

    memset(g_spi_dma_tx_buff, 0, sizeof(g_spi_dma_tx_buff));

    my_log_printf(1, "5050-BT002 LED driver init success");

    // 设置模式7基础模式为3
    my_led_mode7_set_base(3);

    // 启动模式7
    if (my_led_mode_start(7, LED_MAX_PIXELS) != 0)
    {
        my_log_printf(1, "5050-BT002 LED mode7 start failed");
        return -1;
    }

    return 0;
}

/**
 * @brief 启动指定模式动画（10ms节拍）
 */
int my_led_mode_start(uint8 mode_idx, uint8 pixel_cnt)
{
    int i = 0;
    const my_led_mode_t *mode = my_get_mode_by_idx(mode_idx);
    const my_led_mode_t *init_mode = mode;
    if (!mode)
    {
        my_log_printf(1, "invalid mode:%d", mode_idx);
        return -1;
    }
    if (mode_idx == 7)
    {
        init_mode = my_get_mode7_base_mode();
        if (!init_mode)
        {
            my_log_printf(1, "mode7 base invalid");
            return -1;
        }
    }

    g_led_mode_idx = mode_idx;
    g_led_mode_pixel_cnt = my_clamp_pixel_cnt(pixel_cnt, 1);
    g_led_mode_step_cnt = 0;
    for (i = 0; i < 3; i++)
    {
        g_led_mode_cnt[i] = init_mode->init_cnt[i];
    }

    if (g_led_mode_timer_id)
    {
        sys_timer_del(g_led_mode_timer_id);
        g_led_mode_timer_id = 0;
    }
    g_led_mode_timer_id = sys_timer_add(NULL, my_led_mode_scan, MY_LED_MODE_TICK_MS);
    if (!g_led_mode_timer_id)
    {
        my_log_printf(1, "mode start failed");
        return -1;
    }
#if MY_LED_SPI_SPECTRUM_EN
    memset(g_db_data_old, 0, sizeof(g_db_data_old));
    memset(g_led_on_table, 0, sizeof(g_led_on_table));
    g_led_spec_valid = 0;
    g_led_spec_fail_cnt = 0;
    g_led_spec_hit_logged = 0;
#endif
    g_led_mode7_release_on = 0;
    g_led_mode7_release_low_cnt = 0;
    g_led_mode7_global_env = 0;
    my_mode7_spec1_reset_state();
    my_led_mode_scan(NULL);
    if (mode_idx == 7)
    {
        my_log_printf(1, "mode start, idx:%d pixel:%d base:%d", mode_idx, g_led_mode_pixel_cnt, g_led_mode7_base_idx);
    }
    else
    {
        my_log_printf(1, "mode start, idx:%d pixel:%d", mode_idx, g_led_mode_pixel_cnt);
    }
    return 0;
}

/**
 * @brief 设置 mode7 叠加显示时使用的基础模式
 */
int my_led_mode7_set_base(uint8 base_idx)
{
    if (base_idx > 6)
    {
        my_log_printf(1, "mode7 base invalid:%d", base_idx);
        return -1;
    }
    if (!my_get_mode_by_idx(base_idx))
    {
        my_log_printf(1, "mode7 base not found:%d", base_idx);
        return -1;
    }
    g_led_mode7_base_idx = base_idx;
    my_log_printf(1, "mode7 base set:%d", g_led_mode7_base_idx);
    return 0;
}

/**
 * @brief 设置 mode7 亮灯填充风格
 */
int my_led_mode7_set_fill_style(uint8 style)
{
    if (style > 2)
    {
        my_log_printf(1, "mode7 style invalid:%d (0..2)", style);
        return -1;
    }
    g_led_mode7_fill_style = style;
    my_log_printf(1, "mode7 style set:%d", g_led_mode7_fill_style);
    return 0;
}

/**
 * @brief 停止模式动画并清空灯带显示
 */
void my_led_mode_stop(void)
{
    if (g_led_mode_timer_id)
    {
        sys_timer_del(g_led_mode_timer_id);
        g_led_mode_timer_id = 0;
    }
    my_led_set_rgb(0, 0, 0, LED_MAX_PIXELS);
    my_log_printf(1, "mode stop");
}
