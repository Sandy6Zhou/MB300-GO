#ifndef __AURACAST_SINK_API_H__
#define __AURACAST_SINK_API_H__

#include "typedef.h"

// max config
#define MAX_NUM_BIS 1

typedef struct {
    u8 max_bis_num;
} auracast_sink_user_config_t;

enum {
    FRAME_DURATION_7_5,
    FRAME_DURATION_10,
};

typedef struct {
    uint8_t source_mac_addr[6];
    uint8_t broadcast_name[28];
    uint8_t Address_Type;
    uint8_t Advertising_SID;
    s16 sdu_period;
    int sample_rate;
    int bit_rate;
    uint8_t frame_duration;
    uint8_t BIG_Handle;
    uint8_t Num_BIS;
    uint16_t Connection_Handle[2];
} auracast_sink_source_info_t;

enum {
    SOURCE_INFO_REPORT,
    BIG_SYNC_CREATE,
    BIG_SYNC_TERMINATE,
    BIG_SYNC_LOST,
    ISO_RX_CALLBACK,
};

typedef void (*auracast_sink_event_callback_t)(uint16_t event, uint8_t *packet, uint16_t length);

extern int auracast_sink_init(void);
extern int auracast_sink_uninit(void);
extern void auracast_sink_scan_start(void);
extern void auracast_sink_scan_stop(void);
extern int auracast_sink_big_sync_create(auracast_sink_source_info_t *param);
extern int auracast_sink_big_sync_terminate(auracast_sink_source_info_t *param);
extern void auracast_sink_event_callback_register(auracast_sink_event_callback_t callback);

#endif /* __AURACAST_SINK_API_H__ */
