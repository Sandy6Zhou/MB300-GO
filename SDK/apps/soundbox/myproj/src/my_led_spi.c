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
#define MY_LED_SPEC_SAMPLE_TICKS 10
#define MY_LED_SPECTRUM_NODE_NAME "Spectrum1"
#ifndef NODE_UUID_SPECTRUM
#define NODE_UUID_SPECTRUM 0xF538
#endif

static hw_spi_dev g_led_spi_dev;

static uint8 g_spi_dma_tx_buff[LED_DATA_MAX_LEN + LED_RESET_CODE_LEN];
static int g_led_mode_timer_id;
static uint8 g_led_mode_pixel_cnt;
static uint8 g_led_mode_idx;
static uint8 g_led_mode7_base_idx = 1;
static uint8 g_led_mode7_fill_style = 0;
static int g_led_mode_step_cnt;
static int g_led_mode_cnt[3];
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

static uint8 my_clamp_pixel_cnt(int pixel_cnt, uint8 min_val);
static int my_wrap_cnt(int cnt, int prd);
static uint8 my_cnt_to_rgb_val(const my_led_mode_t *mode, int colour, int cnt);
static const my_led_mode_t *my_get_mode_by_idx(uint8 mode_idx);
static const my_led_mode_t *my_get_mode7_base_mode(void);
static int my_get_sec_on_num(const my_led_mode_t *mode, int sec_num);
static u8 my_mode7_should_light(int idx_in_sec, int sec_len, int led_on_num);
#if MY_LED_SPI_SPECTRUM_EN
static void my_led_get_spectrum(void);
#endif

static const my_led_mode_t g_led_modes[] = {
    {
        .mode_idx = 0, .div_sec_num = 6, .sec_led_num = 18, .cnt_freq = 1,
        .rgb_val_max = {255, 255, 255}, .rgb_val_min = {0, 0, 0},
        .init_cnt = {0, 30, 60}, .next_cnt_dir = {-10, -10, -10}, .next_sec_cnt_phase = {0, 0, 0},
        .cnt_unit = {1, 1, 1}, .cnt_max_prd = {90, 90, 90},
        .pu_cnt_start = {0, 0, 0}, .pu_cnt_end = {30, 30, 30},
        .keep_cnt_start = {30, 30, 30}, .keep_cnt_end = {30, 30, 30},
        .pd_cnt_start = {30, 30, 30}, .pd_cnt_end = {60, 60, 60},
    },
    {
        .mode_idx = 1, .div_sec_num = 6, .sec_led_num = 18, .cnt_freq = 1,
        .rgb_val_max = {255, 255, 255}, .rgb_val_min = {0, 0, 0},
        .init_cnt = {0, 36, 72}, .next_cnt_dir = {6, 6, 6}, .next_sec_cnt_phase = {0, 0, 0},
        .cnt_unit = {1, 1, 1}, .cnt_max_prd = {108, 108, 108},
        .pu_cnt_start = {0, 0, 0}, .pu_cnt_end = {0, 0, 0},
        .keep_cnt_start = {0, 0, 0}, .keep_cnt_end = {54, 54, 54},
        .pd_cnt_start = {54, 54, 54}, .pd_cnt_end = {54, 54, 54},
    },
    {
        .mode_idx = 2, .div_sec_num = 6, .sec_led_num = 18, .cnt_freq = 1,
        .rgb_val_max = {255, 255, 255}, .rgb_val_min = {0, 0, 0},
        .init_cnt = {0, 30, 60}, .next_cnt_dir = {-5, -5, -5}, .next_sec_cnt_phase = {0, 0, 0},
        .cnt_unit = {1, 1, 1}, .cnt_max_prd = {90, 90, 90},
        .pu_cnt_start = {0, 0, 0}, .pu_cnt_end = {0, 0, 0},
        .keep_cnt_start = {0, 0, 0}, .keep_cnt_end = {30, 30, 30},
        .pd_cnt_start = {30, 30, 30}, .pd_cnt_end = {30, 30, 30},
    },
    {
        .mode_idx = 3, .div_sec_num = 6, .sec_led_num = 18, .cnt_freq = 1,
        .rgb_val_max = {255, 255, 255}, .rgb_val_min = {0, 0, 0},
        .init_cnt = {0, 36, 72}, .next_cnt_dir = {2, 2, 2}, .next_sec_cnt_phase = {0, 0, 0},
        .cnt_unit = {1, 1, 1}, .cnt_max_prd = {108, 108, 108},
        .pu_cnt_start = {0, 0, 0}, .pu_cnt_end = {0, 0, 0},
        .keep_cnt_start = {0, 0, 0}, .keep_cnt_end = {36, 36, 36},
        .pd_cnt_start = {36, 36, 36}, .pd_cnt_end = {36, 36, 36},
    },
    {
        .mode_idx = 4, .div_sec_num = 6, .sec_led_num = 18, .cnt_freq = 1,
        .rgb_val_max = {255, 255, 255}, .rgb_val_min = {0, 0, 0},
        .init_cnt = {0, 36, 72}, .next_cnt_dir = {-12, -12, -12}, .next_sec_cnt_phase = {0, 0, 0},
        .cnt_unit = {1, 1, 1}, .cnt_max_prd = {108, 108, 108},
        .pu_cnt_start = {0, 0, 0}, .pu_cnt_end = {12, 12, 12},
        .keep_cnt_start = {12, 12, 12}, .keep_cnt_end = {24, 24, 24},
        .pd_cnt_start = {24, 24, 24}, .pd_cnt_end = {36, 36, 36},
    },
    {
        .mode_idx = 5, .div_sec_num = 6, .sec_led_num = 18, .cnt_freq = 1,
        .rgb_val_max = {255, 255, 255}, .rgb_val_min = {0, 0, 0},
        .init_cnt = {0, 36, 72}, .next_cnt_dir = {2, 2, 2}, .next_sec_cnt_phase = {0, 0, 0},
        .cnt_unit = {1, 1, 1}, .cnt_max_prd = {108, 108, 108},
        .pu_cnt_start = {0, 0, 0}, .pu_cnt_end = {0, 0, 0},
        .keep_cnt_start = {0, 0, 0}, .keep_cnt_end = {2, 2, 2},
        .pd_cnt_start = {2, 2, 2}, .pd_cnt_end = {2, 2, 2},
    },
    {
        .mode_idx = 6, .div_sec_num = 4, .sec_led_num = 6, .cnt_freq = 1,
        .rgb_val_max = {255, 128, 0}, .rgb_val_min = {200, 32, 0},
        .init_cnt = {0, 0, 0}, .next_cnt_dir = {-18, 18, 0}, .next_sec_cnt_phase = {18, 0, 0},
        .cnt_unit = {1, 0, 0}, .cnt_max_prd = {48, 108, 0},
        .pu_cnt_start = {0, 0, 0}, .pu_cnt_end = {0, 0, 0},
        .keep_cnt_start = {0, 0, 0}, .keep_cnt_end = {24, 1, 0},
        .pd_cnt_start = {24, 1, 0}, .pd_cnt_end = {24, 108, 0},
    },
    {
        .mode_idx = 7, .div_sec_num = 6, .sec_led_num = 18, .cnt_freq = 1,
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
        return on_num;
    }
    if (mode->div_sec_num > 0)
    {
        short sum0 = 0;
        short sum1 = 0;
        int tmp = 32 / mode->div_sec_num;
        int i = 0;
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
        on_num = ((sum1 * mode->sec_led_num / 90) + sum0);
        if (on_num < 0)
        {
            on_num = 0;
        }
        if (mode->sec_led_num > 0)
        {
            on_num %= mode->sec_led_num;
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
    return (idx_in_sec < led_on_num);
}

#if MY_LED_SPI_SPECTRUM_EN
/**
 * @brief 获取频谱能量并更新mode7使用的能量表
 */
static void my_led_get_spectrum(void)
{
    struct spectrum_parm cfg = {0};
    int ret = -1;
    u8 got_data = 0;
    ret = jlstream_get_node_param(NODE_UUID_SPECTRUM, MY_LED_SPECTRUM_NODE_NAME, &cfg, sizeof(cfg));
    if ((ret == sizeof(cfg)) && cfg.db_data)
    {
        int db_num = cfg.db_num;
        short *db_data = cfg.db_data;
        int j = 0;
        for (j = 0; j < db_num; j++)
        {
            if (j < 32)
            {
                g_led_on_table[j] = db_data[j] - g_db_data_old[j];
                g_db_data_old[j] = db_data[j];
            }
        }
        got_data = 1;
    }

    g_led_spec_valid = got_data;
    if (!got_data)
    {
        g_led_spec_fail_cnt++;
        if ((g_led_spec_fail_cnt % 20) == 0)
        {
            my_log_printf(1, "mode7 spectrum get fail (%d), ret:%d", g_led_spec_fail_cnt, ret);
        }
    }
    else
    {
        if (!g_led_spec_hit_logged)
        {
            my_log_printf(1, "mode7 spectrum hit, uuid:0x%x name:%s", NODE_UUID_SPECTRUM, MY_LED_SPECTRUM_NODE_NAME);
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
    if (style > 1)
    {
        my_log_printf(1, "mode7 style invalid:%d", style);
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
