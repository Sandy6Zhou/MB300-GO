/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : ble_comunication.h
 * Brief      : 设备蓝牙交互模块头文件
 * Version    : V1.0.0
 * Author     : 周森达(zhousenda@jimiiot.com)
 * Date       : 2026-01-14
************************************************************************************************/
#ifndef __BLE_COMUNICATION_H__
#define __BLE_COMUNICATION_H__

#include "my_common.h"

extern uint16 ble_server_mtu;

#define BLE_SERVER_MAX_MTU         517    // MTU最大长度，系统用掉了3个字节,可用数据最大514字节
#define BLE_SERVER_MTU_DFT         23     // MTU默认长度

#define BLE_SERVER_MAX_DATA_LEN    (ble_server_mtu - 3)
#define BLE_SVC_RX_MAX_LEN         (BLE_SERVER_MAX_MTU - 3)
#define BLE_SVC_TX_MAX_LEN         (BLE_SERVER_MAX_MTU - 3)

#define BLE_RESP_LENGTH_MAX         BLE_SVC_RX_MAX_LEN

#define BLE_FRAME_HEAD_LEN          2               // 蓝牙数据帧头长度2字节
#define BLE_CMD_HEAD_LEN            2               // 蓝牙命令头长度2字节
#define BLE_CMD_DATA_LEN_UNIT       16              // 蓝牙命令内容长度基数16字节，因为要加密，必须是16字节的倍数
#define BLE_GATT_FRAME_LEN_MIN      (BLE_FRAME_HEAD_LEN + BLE_CMD_HEAD_LEN + BLE_CMD_DATA_LEN_UNIT)

#define BLE_MTU_DATA_LEN            (256 + 16)

void BLE_DataInputBuffer(const uint8 *data, uint16 len);
void ble_rx_proc_handle(void);
int ble_comu_send_packet(uint16 type, uint8 *data, uint16 len);
void ble_comu_response_or_expansion_cmd(uint16 type, uint8 *str_data, uint8 len);
#endif

