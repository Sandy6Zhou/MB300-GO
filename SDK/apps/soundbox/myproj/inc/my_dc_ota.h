/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : my_dc_ota.h
 * Brief      : DC板OTA模块头文件
 * Version    : V1.0.0
 * Author     : songshiqing(songshiqing@jimiiot.com)
 * Date       : 2026-07-15
 ************************************************************************************************/
#ifndef MY_DC_OTA_H
#define MY_DC_OTA_H

/** @brief 请求使用内置资源固件升级DC，仅用于本地调试。 */
int my_dc_ota_request_start(void);


/** @brief 在DC UART任务中启动升级状态机。 */
int my_dc_ota_start(void);

/** @brief 向升级状态机输入DC串口接收数据。 */
void my_dc_ota_input(const uint8 *data, uint32 len);

/** @brief 驱动DC升级状态机的超时和重试处理。 */
void my_dc_ota_tick(void);

/** @brief 查询DC OTA是否正在占用串口。 */
int my_dc_ota_is_active(void);

/** @brief 获取最近一次YMODEM确认进度。 */
uint8 my_dc_ota_progress(void);

#endif
