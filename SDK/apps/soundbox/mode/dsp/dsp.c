#include "system/includes.h"
#include "app_action.h"
#include "app_main.h"
#include "default_event_handler.h"
#include "tone_player.h"
#include "app_tone.h"
#include "dsp_mode.h"
#include "ui/ui_api.h"
#include "ui/ui_style.h"
#include "ui_manage.h"
#include "local_tws.h"

#define LOG_TAG             "[APP_DSP]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
/* #define LOG_DUMP_ENABLE */
#define LOG_CLI_ENABLE
#include "debug.h"

#if TCFG_APP_DSP_EN

static u8 dsp_idle_flag = 1;
u8 dsp_source_flag = TCFG_DSP_MODE ;

u8 get_dsp_source(void)
{
    return dsp_source_flag;
}

void set_dsp_source(u8 source)
{
    dsp_source_flag = source;
}

static int app_dsp_init()
{
    dsp_idle_flag = 0;
    app_send_message(APP_MSG_ENTER_MODE, APP_MODE_DSP);
    app_send_message(APP_MSG_DSP_OPEN, 0);
    return 0;
}

static void app_dsp_exit()
{
    dsp_idle_flag = 1;
}

struct app_mode *app_enter_dsp_mode(int arg)
{
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SOURCE_EN | LE_AUDIO_AURACAST_SINK_EN))
    int msg[32];
#else
    int msg[16];
#endif
    struct app_mode *next_mode;

    app_dsp_init();

    while (1) {
        if (!app_get_message(msg, ARRAY_SIZE(msg), NULL)) {
            continue;
        }
        next_mode = app_mode_switch_handler(msg);
        if (next_mode) {
            break;
        }

        switch (msg[0]) {
        case MSG_FROM_APP:
            dsp_app_msg_handler(msg + 1);
            break;
        case MSG_FROM_DEVICE:
            break;
        }

        app_default_msg_handler(msg);
    }

    app_dsp_exit();

    return next_mode;
}

static int dsp_mode_try_enter(int arg)
{
    return 0;
}

static int dsp_mode_try_exit()
{
    return 0;
}

static const struct app_mode_ops dsp_mode_ops = {
    .try_enter          = dsp_mode_try_enter,
    .try_exit           = dsp_mode_try_exit,
};

/*
 * 注册dsp模式
 */
REGISTER_APP_MODE(dsp_mode) = {
    .name 	= APP_MODE_DSP,
    .index  = APP_MODE_DSP_INDEX,
    .ops 	= &dsp_mode_ops,
};

static u8 dsp_idle_query(void)
{
    return dsp_idle_flag;
}
REGISTER_LP_TARGET(dsp_lp_target) = {
    .name = "dsp",
    .is_idle = dsp_idle_query,
};

#endif
