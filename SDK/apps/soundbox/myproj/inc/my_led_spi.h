/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : my_led_spi_5050_bt002.h
 * Brief      : bt002灯带驱动模块头文件
 * Version    : V1.0.0
 * Author     : 黄俊凯(huangjunkai@jimiiot.com)
 * Date       : 2026-01-05
************************************************************************************************/
#ifndef _MY_LED_SPI_H_
#define _MY_LED_SPI_H_

// SPI 配置（主机模式，仅使用 DO 引脚传输数据）
#define LED_SPI_CLK_PIN     IO_PORTA_07
#define LED_SPI_DO_PIN      IO_PORTC_05
#define LED_SPI_CLK         8000000L     //spi时钟频率 8MHz

#define LED_PIXEL_BITS      24      // 单个像素点数据长度（G8+R8+B8）
#define LED_MAX_PIXELS      15      // 最大级联像素数

// 复位码长度(Byte)
#define LED_RESET_CODE_LEN  80      // 复位时间(≥80us) / 1bit周期(0.125us) / 8bit
// RGB数据最大长度(Byte)
#define LED_DATA_MAX_LEN    360     // (spi数据(8bit) * rgb数据(24bit) / 8bit) * 级联像素(15个)

void my_led_set_rgb(uint8 red, uint8 green, uint8 blue, uint8 pixel_cnt);

int my_led_5050_bt002_init(uint8 spi);

#endif


