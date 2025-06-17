#ifndef __LE_AURACAST_CONFIG_H__
#define __LE_AURACAST_CONFIG_H__

struct auracast_cfg_t {
    u8 len;
    u8 bn;
    u8 rtn;
    u8 variant;
    u16 sample_rate;
    u32 tx_latency;
    u32 play_latency;
} __attribute__((packed));

#define CONFIG_LE_AUDIO_PARAMS_MAX_NUM 10

struct auracast_cfg_t *get_auracast_cfg_data(u8 mode);

#endif
