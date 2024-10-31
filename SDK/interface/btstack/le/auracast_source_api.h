#ifndef __AURACAST_SOURCE_API_H__
#define __AURACAST_SOURCE_API_H__

#include "typedef.h"

/****
| sampling_frequency| variant  | 采样率 | 帧间隔(us) | 包长(字节) | 码率(kbps) | 重发次数 |
| ----------------- | -------- | ------ | ---------- | -----------| -----------| -------- |
|        0          |    0     | 8000   | 7500       | 26         | 27.732     | 2        |
|        0          |    1     | 8000   | 10000      | 30         | 24         | 2        |
|        1          |    0     | 16000  | 7500       | 30         | 32         | 2        |
|        1          |    1     | 16000  | 10000      | 40         | 32         | 2        |
|        2          |    0     | 24000  | 7500       | 45         | 48         | 2        |
|        2          |    1     | 24000  | 10000      | 60         | 48         | 2        |
|        3          |    0     | 32000  | 7500       | 60         | 64         | 2        |
|        3          |    1     | 32000  | 10000      | 80         | 64         | 2        |
|        4          |    0     | 44100  | 8163       | 97         | 95.06      | 4        |
|        4          |    1     | 44100  | 10884      | 130        | 95.55      | 4        |
|        5          |    0     | 48000  | 7500       | 75         | 80         | 4        |
|        5          |    1     | 48000  | 10000      | 100        | 80         | 4        |
****/

typedef struct {
    uint8_t config_num_bis;             //配置bis的数量
    uint8_t config_sampling_frequency;  //配置支持的采样率序号
    uint8_t config_variant;             //配置采样率之外其他参数类型
    uint8_t encryption;
    uint8_t broadcast_code[16];
    char    broadcast_name[33];         // 长度要求 4 byte 到 32 byte
    uint32_t broadcast_id;              // 24 bits
} auracast_user_config_t;

// event
// event 0-15 -> BIS index  send callback
#define AURACAST_SOURCE_BIG_CREATED         (0x0010)
#define AURACAST_SOURCE_BIG_TERMINATED      (0x0011)

typedef void (*auracast_source_event_callback_t)(uint16_t event, uint8_t *packet, uint16_t length);

extern int auracast_source_init();
extern void auracast_source_config(auracast_user_config_t *param);
extern void auracast_source_event_callback_register(auracast_source_event_callback_t callback);
extern void auracast_source_start(void);
extern void auracast_source_stop(void);
extern int auracast_source_uninit();

extern uint16_t auracast_source_get_bis_hdl(uint8_t bis_index);
extern u32 auracast_source_read_iso_tx_sync(uint8_t bis_index);
extern u32 auracast_source_get_sync_delay(void);

#endif


