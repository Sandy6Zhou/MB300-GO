#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".pawr.data.bss")
#pragma data_seg(".pawr.data")
#pragma const_seg(".pawr.text.const")
#pragma code_seg(".pawr.text")
#endif
/**
 * @file ble_pawr_demo.c
 * @brief BLE Periodic Advertising with Responses (PAwR) Demo Implementation
 *
 * This file demonstrates how to use the PAwR API for both transmitter and receiver roles.
 * It includes initialization, configuration, and basic operation examples.
 */
#include "system/includes.h"
#include "le_auracast_pawr.h"
#include "app_config.h"

#define LOG_TAG             "[PAWR]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
#define LOG_CLI_ENABLE
#include "debug.h"

/* PAwR Transmitter Configuration */
#define PAWR_TX_SUBEVENT_COUNT           2           // Total number of subevents
#define PAWR_TX_RESPONSE_SLOTS           5           // Number of response slots per subevent

#define PAWR_TX_SLOT_SPACING             2           // Slot spacing in 0.125 ms units (2 = 250us)
#define PAWR_TX_CHANNEL_INDEX            30           // fix channel index
#define PAWR_TX_RESPONSE_ACCESS_ADDRESS  0x692e0e53  // Example AA for responses
#define PAWR_TX_VENDOR_OP_PADV_FIELD     0xEF

/* PAwR Receiver Configuration */
#define PAWR_RX_TARGET_SUBEVENT         0           // Subevent index to respond in (0-based)
#define PAWR_RX_TARGET_SLOT             0           // Slot index to respond in (0-based)

static u8 pawr_tx_num_synced;

static const uint8_t pawr_rx_adv_data[] = {
    0x05, 0x09, 'P', 'A', 'w', 'R',         // Complete Local Name: "PAwR"
};

static const uint8_t pawr_tx_vendor_padv_data[] = {
    0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC          // Manufacturer Specific Data
};

void app_speaker_set_pa_response_data(const uint8_t *data, uint8_t data_len)
{
    log_info("update PAwR Receiver data...");

    if (data_len > sizeof(pawr_rx_adv_data))  {
        log_info("update rx data too long");
        return;
    }
    pawr_api.rx_rsp_adv_update(data, data_len);
}

/**
 * @brief Initialize PAwR Transmitter functionality
 *
 * This function sets up the device as a PAwR transmitter with the configured parameters.
 * @brief Update PAwR Transmitter advertising data
 *
 * @param data Pointer to new advertising data
 * @param len Length of new advertising data
 */
static void ble_pawr_tx_update_adv_data(const uint8_t *data, uint16_t len)
{
    log_info("Updating PAwR advertising data");
    if (len > sizeof(pawr_tx_vendor_padv_data) - 1)  {
        log_info("update tx data too long");
        return;
    }
    // Update a specific field in the advertising data
    // Note: You would typically update a specific AD type (e.g., manufacturer data)
    pawr_api.tx_update_adv_field(PAWR_TX_VENDOR_OP_PADV_FIELD, data, len);
}

/**
 * @brief PAwR TX data callback function
 *
 * This function is called when PAwR response data is received by the transmitter.
 *
 * @param packet Received data packet
 * @param size   Length of the received data
 */

static void ble_pawr_tx_data_callback(uint8_t *packet, uint16_t size)
{
    log_info("pawr tx rev:%d", size - 1);
    packet++; // skip header
    put_buf(packet, size - 1);
    // 更新pawr ap方数据示例
    /* static uint8_t tx_data_index = 0; */
    /* const uint8_t tx_data_len = sizeof(pawr_tx_vendor_padv_data1); */
    /* ble_pawr_tx_update_adv_data(tx_data_list[tx_data_index], tx_data_len); */
    /* tx_data_index = (tx_data_index + 1) % (sizeof(tx_data_list) / sizeof(tx_data_list[0])); */
}

int ble_pawr_rx_parse_data(uint8_t *data, size_t data_size, uint8_t *output, u32 *output_size)
{
    /* putchar('@'); */
    for (uint16_t i = data_size; i > 0; i--) {
        /* printf("0x%02x", data[i-1]); */
        if (data[i - 1] == PAWR_TX_VENDOR_OP_PADV_FIELD) {
            if (i < 2) {
                return 0;
            }
            uint8_t length = data[i - 2];

            memcpy(output, &data[i - 1], length);
            *output_size = length;

            return 1;
        }
    }

    return 0;
}

/**
 * @brief PAwR RX data callback function
 *
 * This function is called when PAwR advertising data is received by the receiver.
 *
 * @param packet Received advertising data
 * @param size   Length of the received data
 */
static void app_speaker_pawr_scan_callback(uint8_t *packet, uint16_t size)
{
    uint8_t output[32] = {0};
    uint32_t output_size = 0;

    if (ble_pawr_rx_parse_data(packet, size, output, &output_size)) {
        put_buf(output, output_size);
        log_info("pawr rx len %d", output_size);

        // 更新rsp数据示例
        /* static uint8_t index = 0; */
        /* const uint8_t data_len = sizeof(pawr_rx_adv_data1); */
        /* app_speaker_set_pa_response_data(rx_data_list[index], data_len); */
        /* index = (index + 1) % (sizeof(rx_data_list) / sizeof(rx_data_list[0])); */
    }
}

void ble_pawr_get_sync_coord(pawr_timing_coord_t *coord)
{
    coord->subevent      = pawr_tx_num_synced / PAWR_TX_RESPONSE_SLOTS;
    coord->response_slot = pawr_tx_num_synced % PAWR_TX_RESPONSE_SLOTS;
    coord->is_full = 0;
    log_info("[PAWR Sync Coord] sync_idx=%d → subevent=%d, response_slot=%d\n",
             pawr_tx_num_synced, coord->subevent, coord->response_slot);
    pawr_tx_num_synced++;

    if (pawr_tx_num_synced == PAWR_TX_SUBEVENT_COUNT * PAWR_TX_RESPONSE_SLOTS) {
        coord->is_full = 1;
    }
}
/**
 * @brief Shutdown PAwR Transmitter functionality
 */
void app_speaker_stop_pawr(void)
{
    log_info("Shutting down PAwR Transmitter...");
    ble_pawr_tx_acl_scan_hw_reg(0);

    log_info("%s\n", __FUNCTION__);

#if (THIRD_PARTY_PROTOCOLS_SEL & MULTI_CLIENT_EN)
    ble_client_module_enable(0);
#endif
    pawr_tx_num_synced = 0;
    pawr_api.tx_close();
}

void app_speaker_start_pawr(void)
{
#if (THIRD_PARTY_PROTOCOLS_SEL & MULTI_CLIENT_EN)
    // open pawr acl master
    ble_client_module_enable(1);
#endif
    log_info("Initializing PAwR Transmitter...");

    // Register TX callback function
    pawr_api.tx_set_callback(ble_pawr_tx_data_callback);
    pawr_api.tx_add_adv_field(PAWR_TX_VENDOR_OP_PADV_FIELD, pawr_tx_vendor_padv_data, sizeof(pawr_tx_vendor_padv_data));
    // Initialize PAwR transmitter with configured parameters
    pawr_api.tx_open(
        PAWR_TX_RESPONSE_ACCESS_ADDRESS,
        PAWR_TX_RESPONSE_SLOTS,
        PAWR_TX_SLOT_SPACING,
        PAWR_TX_SUBEVENT_COUNT,
        PAWR_TX_CHANNEL_INDEX
    );

    log_info("PAwR Transmitter initialized:");
    log_info("- Response AA: 0x%08X", PAWR_TX_RESPONSE_ACCESS_ADDRESS);
    log_info("- %d response slots per subevent", PAWR_TX_RESPONSE_SLOTS);
    log_info("- Slot spacing: %d * 1.25ms", PAWR_TX_SLOT_SPACING);
    log_info("- %d total subevents", PAWR_TX_SUBEVENT_COUNT);
    log_info("- Channel index: %d", PAWR_TX_CHANNEL_INDEX);
}

/**
 * @brief Initialize PAwR Receiver functionality
 *
 * This function sets up the device as a PAwR receiver with the configured parameters.
 */
void app_speaker_create_pawr_sync(u8 sub_num, u8 slot_num)
{
    log_info("Initializing PAwR Receiver...");
    /* log_info("fix sub_num :%d, slot_num:%d", sub_num, slot_num); */
    // Register RX callback function
    pawr_api.rx_set_callback(app_speaker_pawr_scan_callback);

    // Initialize PAwR receiver with configured parameters
    pawr_api.rx_init(
        sub_num,
        slot_num,
        PAWR_TX_CHANNEL_INDEX,
        sizeof(pawr_rx_adv_data),
        pawr_rx_adv_data,
        PAWR_TX_SLOT_SPACING,
        PAWR_TX_RESPONSE_ACCESS_ADDRESS
    );

    log_info("PAwR Receiver initialized:");

    log_info("- Response AA: 0x%08X", PAWR_TX_RESPONSE_ACCESS_ADDRESS);
    log_info("- Target subevent: %d", sub_num);
    log_info("- Target slot: %d", slot_num);
    log_info("- Channel index: %d", PAWR_TX_CHANNEL_INDEX);
    log_info("- Adv data length: %d", sizeof(pawr_rx_adv_data));
}

/**
 * @brief Shutdown PAwR Receiver functionality
 */
void app_speaker_cancle_pawr_sync(void)
{
    log_info("Shutting down PAwR Receiver...");
    pawr_api.rx_close();
    pawr_slave_exit();
}

void ble_pawr_tx_acl_scan_hw_reg(u8 en)
{
    pawr_api.tx_acl_scan_hw_get_reg(en);
}

void ble_pawr_rx_acl_adv_hw_reg(u8 en)
{
    pawr_api.rx_acl_adv_hw_get_reg(en);
}

