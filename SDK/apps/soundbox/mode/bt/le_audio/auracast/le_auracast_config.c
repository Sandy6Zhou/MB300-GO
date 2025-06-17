#include "app_config.h"
#include "app_main.h"
#include "le_auracast_config.h"

const static u8 platform_data_mapping[] = {
    APP_MODE_BT,
    APP_MODE_MUSIC,
    APP_MODE_LINEIN,
    APP_MODE_PC,
    APP_MODE_IIS,
    APP_MODE_MIC,
#ifndef CONFIG_CPU_BR29
    APP_MODE_SPDIF,
#endif
    APP_MODE_FM,
};

static struct auracast_cfg_t args[CONFIG_LE_AUDIO_PARAMS_MAX_NUM];

struct auracast_cfg_t *get_auracast_cfg_data(u8 mode)
{
    u8 find = 0;
    u8 index = 0;

    int len = syscfg_read(CFG_AURACAST_PARAMS, args, sizeof(args));

    if (len <= 0) {
        r_printf("ERR:Can not read the broadcast config\n");
        return NULL;
    }

    if (mode == APP_MODE_SINK) {
        mode = APP_MODE_BT;
    }

    for (index = 0; index < ARRAY_SIZE(platform_data_mapping); index++) {
        if (mode == platform_data_mapping[index]) {
            find = 1;
            break;
        } else {
        }
    }

    if (!find) {
        r_printf("ERR:Can not find the broadcast config\n");
        return NULL;
    }

    printf("bn:%d\n", args[index].bn);
    printf("rtn:%d\n", args[index].rtn);
    printf("variant:%d\n", args[index].variant);
    printf("sample_rate:%d\n", args[index].sample_rate);
    printf("tx_letency:%d\n", args[index].tx_latency);
    printf("play_letency:%d\n", args[index].play_latency);
    /* put_buf(&args[index], sizeof(struct auracast_cfg_t)); */
    return &args[index];
}

