#ifndef BLE_PAWR_DEMO_H
#define BLE_PAWR_DEMO_H

#include "system/includes.h"

typedef struct pawr_api_s {
    void (*tx_open)(uint32_t rspaa, uint8_t rsp_slots, uint8_t rsp_slot_space, uint8_t sub_nums, uint8_t freq);
    void (*tx_close)(void);
    void (*tx_set_callback)(void (*cb)(uint8_t *packet, uint16_t size));
    void (*tx_update_adv_field)(u8 type, const u8 *data, u8 data_len);
    void (*tx_add_adv_field)(u8 type, const u8 *data, u8 data_len);
    void (*tx_acl_scan_hw_get_reg)(u8 enable);

    void (*rx_set_callback)(void (*cb)(uint8_t *packet, uint16_t size));
    void (*rx_init)(uint8_t sub_index, uint8_t sub_slot, uint8_t freq_idx, uint8_t adv_len, const uint8_t *adv_data, u8 rsp_slot_space, uint32_t rspaa);
    void (*rx_close)(void);
    int (*rx_rsp_adv_update)(const uint8_t *data, uint8_t len);
    void (*rx_acl_adv_hw_get_reg)(u8 enable);
} pawr_api_t;


typedef struct {
    uint8_t type;
    const uint8_t *data;
    uint8_t length;
} adv_field_t;

typedef struct {
    uint8_t is_full;
    uint8_t subevent;
    uint8_t response_slot;
} pawr_timing_coord_t;


extern const pawr_api_t pawr_api;

extern void app_speaker_create_pawr_sync(u8 sub_num, u8 slot_num);
extern void app_speaker_cancle_pawr_sync(void);
extern void app_speaker_start_pawr(void);
extern void app_speaker_stop_pawr(void);
extern void pawr_slave_exit(void);
extern void pawr_slave_init(void);
extern void ble_pawr_rx_acl_adv_hw_reg(u8 en);
extern void ble_pawr_tx_acl_scan_hw_reg(u8 en);
void ble_client_module_enable(u8 en);
#endif /* BLE_PAWR_DEMO_H */
