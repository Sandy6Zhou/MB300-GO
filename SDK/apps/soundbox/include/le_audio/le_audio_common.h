/*************************************************************************************************/
/*!
*  \file      le_audio_common.h
*
*  \brief
*
*  Copyright (c) 2011-2024 ZhuHai Jieli Technology Co.,Ltd.
*
*/
/*************************************************************************************************/
#ifndef __LE_AUDIO_COMMON_H__
#define __LE_AUDIO_COMMON_H__

#include "typedef.h"
#include "app_config.h"

int le_audio_scene_deal(int scene);

int update_le_audio_deal_scene(int scene);

u8 get_le_audio_app_mode_exit_flag();

u8 get_le_audio_curr_role(void); //1:transmitter; 2:recevier

u8 get_le_audio_switch_onoff(void); //1:on; 0:off

void le_audio_common_init(void);

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SOURCE_EN | LE_AUDIO_AURACAST_SINK_EN)) || \
    (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_UNICAST_SOURCE_EN | LE_AUDIO_UNICAST_SINK_EN)) || \
    (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN)) || \
    (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_CIS_CENTRAL_EN | LE_AUDIO_JL_CIS_PERIPHERAL_EN))
void read_le_audio_product_name(void);

void read_le_audio_pair_name(void);

const char *get_le_audio_product_name(void);

const char *get_le_audio_pair_name(void);

int le_audio_ops_register(u8 mode);

int le_audio_ops_unregister(void);

void le_audio_working_status_switch(u8 en);

void le_audio_all_restart(void);

void le_audio_tws_sync_mic_status(void);




int get_ble_report_addr_is_recorded(u8 *mac_addr);

int ble_resolve_adv_addr_mode(void);

void get_curr_ble_filter_adv_addr(u8 *filter_addr);

u8 get_adv_report_addr_filter_start_flag(void);

void set_curr_ble_filter_adv_addr(u8 *filter_addr);

int le_audio_start_record_mac_addr(void);

int le_audio_stop_record_mac_addr(void);

bool ble_adv_report_match_by_addr(u8 *addr);

int le_audio_switch_source_device(u8 switch_mode); //0:切换设备后不过滤设备；1：切换设备后过滤处理只连接记录的设备

#endif
#endif

