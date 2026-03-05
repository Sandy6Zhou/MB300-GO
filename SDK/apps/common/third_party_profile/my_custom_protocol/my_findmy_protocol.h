/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : my_findmy_protocol.h
 * Brief      : 设备自定义findmy协议模块头文件
 * Version    : V1.0.0
 * Author     : 周森达(zhousenda@jimiiot.com)
 * Date       : 2025-12-22
************************************************************************************************/

#ifndef _MY_FINDMY_PROTOCOL_H_
#define _MY_FINDMY_PROTOCOL_H_

#include "system/includes.h"

#define GOOGLE_ADV_ID   0
#define APPLE_ADV_ID    1

typedef struct {
    void *handle;   //创建的对象句柄
    u8 valid;       //当前广播对象是否处于启用状态，默认四个广播都是启动状态
} ADV_HDL_S;

void my_findmy_init(void);
void my_findmy_exit(void);
int my_findmy_adv_enable(u8 enable);
int my_findmy_ble_google_send(u8 *data, u32 len);
int my_findmy_ble_ios_send(u8 *data, u32 len);
int custom_demo_spp_send(u8 *data, u32 len);
void set_adv_valid_status(int index, int status);
void ble_server_send_notification(u8 *data, u16 tx_len);
bool check_connect_id_enable(void);

#endif

