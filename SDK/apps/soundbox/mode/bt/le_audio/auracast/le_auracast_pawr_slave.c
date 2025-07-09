#include "sdk_config.h"
#include "app_msg.h"
// #include "earphone.h"
#include "app_main.h"
#include "btstack/avctp_user.h"
#include "multi_protocol_main.h"
#include "le_auracast_pawr_slave_profile.h"
#include "le_auracast_pawr.h"

#define LOG_TAG             "[PAWR_SLAVE]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
#define LOG_CLI_ENABLE
#include "debug.h"


static char pawr_device_name[30] = "pawr_slave";

void *pawr_slave_demo_ble_hdl = NULL;

static int pawr_slave_demo_ble_send(u8 *data, u32 len);
static int pawr_slave_demo_adv_enable(u8 enable);
static void pawr_slave_demo_ble_disconnect(void);
/*************************************************
                  BLE 相关内容
*************************************************/
static u16 pawr_slave_adv_interval_min = 150;

static void pawr_slave_cbk_packet_handler(void *hdl, uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    u16 con_handle;
    // printf("cbk packet_type:0x%x, packet[0]:0x%x, packet[2]:0x%x", packet_type, packet[0], packet[2]);
    switch (packet_type) {
    case HCI_EVENT_PACKET:
        switch (hci_event_packet_get_type(packet)) {
        case ATT_EVENT_CAN_SEND_NOW:
            printf("ATT_EVENT_CAN_SEND_NOW");
            break;

        case HCI_EVENT_LE_META:
            switch (hci_event_le_meta_get_subevent_code(packet)) {
            case HCI_SUBEVENT_LE_CONNECTION_COMPLETE:
                con_handle = little_endian_read_16(packet, 4);
                printf("HCI_SUBEVENT_LE_CONNECTION_COMPLETE: %0x", con_handle);
                // reverse_bd_addr(&packet[8], addr);
                put_buf(&packet[8], 6);
                break;
            default:
                break;
            }
            break;

        case HCI_EVENT_DISCONNECTION_COMPLETE:
            printf("HCI_EVENT_DISCONNECTION_COMPLETE: %0x", packet[5]);
            /* pawr_slave_demo_adv_enable(1); */
            break;
        default:
            break;
        }
        break;
    }
    return;
}

static uint16_t pawr_slave_att_read_callback(void *hdl, hci_con_handle_t connection_handle, uint16_t att_handle, uint16_t offset, uint8_t *buffer, uint16_t buffer_size)
{
    uint16_t  att_value_len = 0;
    uint16_t handle = att_handle;

    log_info("read_callback,conn_handle =%04x, handle=%04x,buffer=%08x\n", connection_handle, handle, (uint32_t)buffer);

    switch (handle) {
    case ATT_CHARACTERISTIC_2a00_01_VALUE_HANDLE: {
        const char *gap_name = pawr_device_name;
        att_value_len = strlen(gap_name);

        if ((offset >= att_value_len) || (offset + buffer_size) > att_value_len) {
            break;
        }

        if (buffer) {
            memcpy(buffer, &gap_name[offset], buffer_size);
            att_value_len = buffer_size;
            log_info("\n------read gap_name: %s\n", gap_name);
        }
    }
    break;

    case ATT_CHARACTERISTIC_ae10_01_VALUE_HANDLE:
        break;

    case ATT_CHARACTERISTIC_ae04_01_CLIENT_CONFIGURATION_HANDLE:
    case ATT_CHARACTERISTIC_ae02_01_CLIENT_CONFIGURATION_HANDLE:
    case ATT_CHARACTERISTIC_ae05_01_CLIENT_CONFIGURATION_HANDLE:
    case ATT_CHARACTERISTIC_ae3c_01_CLIENT_CONFIGURATION_HANDLE:
    case ATT_CHARACTERISTIC_2a05_01_CLIENT_CONFIGURATION_HANDLE:
        if (buffer) {
            buffer[0] = att_get_ccc_config(handle);
            buffer[1] = 0;
        }
        att_value_len = 2;
        break;

    default:
        break;
    }

    log_info("att_value_len= %d\n", att_value_len);
    return att_value_len;

}

static int pawr_slave_att_write_callback(void *hdl, hci_con_handle_t connection_handle, uint16_t att_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size)
{
    int result = 0;
    /* uint16_t tmp16; */

    uint16_t handle = att_handle;

    log_info("write_callback,conn_handle =%04x, handle =%04x,size =%d\n", connection_handle, handle, buffer_size);

    switch (handle) {

    case ATT_CHARACTERISTIC_2a00_01_VALUE_HANDLE:
        break;

    case ATT_CHARACTERISTIC_ae02_01_CLIENT_CONFIGURATION_HANDLE:
    case ATT_CHARACTERISTIC_ae04_01_CLIENT_CONFIGURATION_HANDLE:
    case ATT_CHARACTERISTIC_ae05_01_CLIENT_CONFIGURATION_HANDLE:
    case ATT_CHARACTERISTIC_ae3c_01_CLIENT_CONFIGURATION_HANDLE:
    case ATT_CHARACTERISTIC_2a05_01_CLIENT_CONFIGURATION_HANDLE:
        log_info("\n------write ccc:%04x,%02x\n", handle, buffer[0]);
        att_set_ccc_config(handle, buffer[0]);
        break;

    case ATT_CHARACTERISTIC_ae10_01_VALUE_HANDLE:
        break;

    case ATT_CHARACTERISTIC_ae01_01_VALUE_HANDLE:
        // 配置pawr rx sync插槽位置
        pawr_timing_coord_t *coord_ptr = (pawr_timing_coord_t *)buffer;
        log_info("pawr slave rx fix slot info:%d-%d", coord_ptr->subevent, coord_ptr->response_slot);
        app_speaker_create_pawr_sync(coord_ptr->subevent, coord_ptr->response_slot);
        pawr_slave_demo_ble_disconnect();
        break;

    case ATT_CHARACTERISTIC_ae03_01_VALUE_HANDLE:
        log_info("\n-ae_rx(%d):", buffer_size);
        put_buf(buffer, buffer_size);

        break;

        log_info("------write ccc:%04x,%02x\n", handle, buffer[0]);

        att_set_ccc_config(handle, buffer[0]);
        break;

    case ATT_CHARACTERISTIC_ae3b_01_VALUE_HANDLE:
        log_info("\n-ae3b_rx(%d):", buffer_size);
        put_buf(buffer, buffer_size);
        break;

    default:
        break;
    }
    return 0;

}

static u8 pawr_slave_fill_adv_data(u8 *adv_data)
{
    u8 offset = 0;
    const char *name_p = pawr_device_name;
    int name_len = strlen(name_p);
    offset += make_eir_packet_data(&adv_data[offset], offset, HCI_EIR_DATATYPE_COMPLETE_LOCAL_NAME, (void *)name_p, name_len);
    if (offset > ADV_RSP_PACKET_MAX) {
        puts("***rsp_data overflow!!!!!!\n");
        return 0;
    }
    return offset;
}

static u8 pawr_slave_fill_rsp_data(u8 *rsp_data)
{
    return 0;
}

static int pawr_slave_demo_adv_enable(u8 enable)
{
    uint8_t adv_type = ADV_IND;
    uint8_t adv_channel = ADV_CHANNEL_ALL;
    uint8_t advData[ADV_RSP_PACKET_MAX] = {0};
    uint8_t rspData[ADV_RSP_PACKET_MAX] = {0};

    if (enable == app_ble_adv_state_get(pawr_slave_demo_ble_hdl)) {
        return 0;
    }
    if (enable) {
        uint8_t len = 0;
        app_ble_set_adv_param(pawr_slave_demo_ble_hdl, pawr_slave_adv_interval_min, adv_type, adv_channel);
        len = pawr_slave_fill_adv_data(advData);
        if (len) {
            app_ble_adv_data_set(pawr_slave_demo_ble_hdl, advData, len);
        }
        len = pawr_slave_fill_rsp_data(rspData);
        if (len) {
            app_ble_rsp_data_set(pawr_slave_demo_ble_hdl, rspData, len);
        }
    }
    app_ble_adv_enable(pawr_slave_demo_ble_hdl, enable);
    return 0;
}

static int pawr_slave_demo_ble_send(u8 *data, u32 len)
{
    int ret = 0;
    /* int i; */
    log_info("pawr_slave_demo_ble_send len = %d", len);
    put_buf(data, len);
    ret = app_ble_att_send_data(pawr_slave_demo_ble_hdl, ATT_CHARACTERISTIC_ae02_01_VALUE_HANDLE, data, len, ATT_OP_AUTO_READ_CCC);
    if (ret) {
        log_info("send fail\n");
    }
    return ret;
}

void pawr_slave_init(void)
{
    log_info("pawr_slave_demo_init\n");

    ble_pawr_rx_acl_adv_hw_reg(1);
    // add vendor adv config
    const uint8_t *edr_addr = bt_get_mac_addr();
    printf("edr addr:");
    put_buf((uint8_t *)edr_addr, 6);

    // BLE init
    if (pawr_slave_demo_ble_hdl == NULL) {
        pawr_slave_demo_ble_hdl = app_ble_hdl_alloc();
        if (pawr_slave_demo_ble_hdl == NULL) {
            printf("pawr_slave_demo_ble_hdl alloc err !\n");
            return;
        }
        app_ble_set_mac_addr(pawr_slave_demo_ble_hdl, (void *)edr_addr);
        app_ble_profile_set(pawr_slave_demo_ble_hdl, multi_profile_data);
        app_ble_att_read_callback_register(pawr_slave_demo_ble_hdl, pawr_slave_att_read_callback);
        app_ble_att_write_callback_register(pawr_slave_demo_ble_hdl, pawr_slave_att_write_callback);
        app_ble_att_server_packet_handler_register(pawr_slave_demo_ble_hdl, pawr_slave_cbk_packet_handler);
        app_ble_hci_event_callback_register(pawr_slave_demo_ble_hdl, pawr_slave_cbk_packet_handler);
        app_ble_l2cap_packet_handler_register(pawr_slave_demo_ble_hdl, pawr_slave_cbk_packet_handler);

        pawr_slave_demo_adv_enable(1);
    }
    // BLE init end

}

static void pawr_slave_demo_ble_disconnect(void)
{
    if (app_ble_get_hdl_con_handle(pawr_slave_demo_ble_hdl)) {
        app_ble_disconnect(pawr_slave_demo_ble_hdl);
    }
}

void pawr_slave_exit(void)
{
    log_info("pawr_slave_demo_exit\n");
    ble_pawr_rx_acl_adv_hw_reg(0);
    // BLE exit
    if (app_ble_get_hdl_con_handle(pawr_slave_demo_ble_hdl)) {
        app_ble_disconnect(pawr_slave_demo_ble_hdl);
    } else {
        pawr_slave_demo_adv_enable(0);
    }

    if (pawr_slave_demo_ble_hdl) {
        app_ble_hdl_free(pawr_slave_demo_ble_hdl);
        pawr_slave_demo_ble_hdl = NULL;
    }

}


