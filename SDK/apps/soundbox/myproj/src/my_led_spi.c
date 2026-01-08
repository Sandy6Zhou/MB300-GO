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

static hw_spi_dev g_led_spi_dev;

static uint8 g_spi_dma_tx_buff[LED_DATA_MAX_LEN + LED_RESET_CODE_LEN];

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
        my_log_printf(1, "%s(): SPI(%d) DMA tx finish, len:%d", __func__, spi, (-1 * hw_spix_get_isr_len(spi)));
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

    memset(g_spi_dma_tx_buff, 0, sizeof(g_spi_dma_tx_buff));

    for (i = 0; i < LED_MAX_PIXELS; i++)
    {
        // 编码像素数据
        my_led_encode_pixel(&g_spi_dma_tx_buff[i * LED_PIXEL_BITS], g[i], r[i], b[i]);
    }

    // 发送数据到DMA
    spi_dma_transmit_for_isr(g_led_spi_dev, g_spi_dma_tx_buff, sizeof(g_spi_dma_tx_buff), 0);
    return 0;
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

    if (pixel_cnt > LED_MAX_PIXELS)
    {
        pixel_cnt = LED_MAX_PIXELS;
    }
    else if (pixel_cnt < 0)
    {
        pixel_cnt = 0;
    }

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