/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : my_ble_ota.h
 * Brief      : BLE接收DC升级文件模块头文件
 * Version    : V1.0.0
 * Author     : songshiqing(songshiqing@jimiiot.com)
 * Date       : 2026-07-16
 ************************************************************************************************/
#ifndef MY_BLE_OTA_H
#define MY_BLE_OTA_H

#define MY_BLE_OTA_STORAGE_PATH "flash/app/DCOTA" /* DC固件预留区资源路径 */
#define MY_BLE_OTA_STORAGE_SIZE 0x10000u          /* DCOTA预留区容量：64KB */

/** @brief 在BLE任务中处理解密后的0x4605文件传输数据。 */
void my_ble_ota_handle_packet(const uint8 *data, uint16 len);

/** @brief 处理BLE文件传输超时。 */
void my_ble_ota_tick(void);

#endif
