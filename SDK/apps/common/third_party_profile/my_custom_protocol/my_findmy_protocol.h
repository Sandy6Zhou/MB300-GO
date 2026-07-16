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
void ble_server_on_can_send_now(void); /* BLE 线程：flush 积压并尝试 schedule EMS */
bool check_connect_id_enable(void);
void ble_defer_ems_schedule_after_cid_auth(void); // CID成功发5505后调用，5505发完再schedule EMS
void ble_cancel_defer_ems_schedule(void);
void ble_dc_power_off_handle(void);
void ble_dc_power_on_restore(void);
void ble_dc_unbind_handle(void);
void ble_connect_api(void);
void ble_disconnect_api(void);
void bt_connect_api(void);
void bt_disconnect_api(void);
u8 ble_dc_is_power_suppressed(void);

#endif

