#include "app_main.h"
#include "system/includes.h"
#include "app_action.h"
#include "default_event_handler.h"
#include "soundbox.h"
#include "app_tone.h"
#include "gpio_config.h"
#include "audio_config.h"
#include "media/includes.h"
#include "iis_file.h"
#include "audio_iis.h"
#include "loudspeaker_iis_player.h"
#include "loudspeaker.h"

#if TCFG_APP_LOUDSPEAKER_EN

extern const struct key_remap_table loudspeaker_mode_key_table[];

static u8 loudspeaker_idle_flag = 1;
u8 loudspeaker_source_flag = 1;
#if TCFG_MIC_EFFECT_ENABLE
// 混响开关策略：mic模式下和混响不能同时存在
static u8 mic_effect_open_flag = 0;		//记录混响开关状态
#endif

u8 get_loudspeaker_source(void)
{
    return loudspeaker_source_flag;
}

void set_loudspeaker_source(u8 source)
{
    loudspeaker_source_flag = source;
}

static int loudspeaker_tone_play_end_callback(void *priv, enum stream_event event)
{
    if (false == app_in_mode(APP_MODE_LOUDSPEAKER)) {
        return 0;
    }
    switch (event) {
    case STREAM_EVENT_NONE:
    case STREAM_EVENT_STOP:
        app_send_message(APP_MSG_LOUDSPEAKER_OPEN, 0);
        break;
    default:
        break;
    }
    return 0;
}

void app_loudspeaker_exit()
{
    loudspeaker_stop();

    loudspeaker_idle_flag = 1;
    app_send_message(APP_MSG_EXIT_MODE, APP_MODE_LOUDSPEAKER);
}


static int app_loudspeaker_init(void)
{

    loudspeaker_idle_flag = 0;
    tone_player_stop();

    int ret;
    ret = play_tone_file_callback(get_tone_files()->mic_mode, NULL, loudspeaker_tone_play_end_callback);

    if (ret) {
        //提示音播放失败
        loudspeaker_tone_play_end_callback(NULL, STREAM_EVENT_NONE);
    }

#ifdef TCFG_PITCH_SPEED_NODE_ENABLE
    app_var.pitch_mode = PITCH_0;    //设置变调初始模式
#endif

#if (LEA_BIG_CTRLER_TX_EN || LEA_BIG_CTRLER_RX_EN || LEA_CIG_CENTRAL_EN || LEA_CIG_PERIPHERAL_EN)
    btstack_init_in_other_mode();
#if (LEA_BIG_FIX_ROLE==2)
    loudspeaker_set_broadcast_local_open_flag(1);
#endif
#endif
    app_send_message(APP_MSG_ENTER_MODE, APP_MODE_LOUDSPEAKER);
    return 0;
}

struct app_mode *app_enter_loudspeaker_mode(int arg)
{
    int msg[16];
    struct app_mode *next_mode;

    app_loudspeaker_init();

    while (1) {
        if (!app_get_message(msg, ARRAY_SIZE(msg), loudspeaker_mode_key_table)) {
            continue;
        }
        next_mode = app_mode_switch_handler(msg);
        if (next_mode) {
            break;
        }

        switch (msg[0]) {
        case MSG_FROM_APP:
            loudspeaker_app_msg_handler(msg + 1);
            break;
        case MSG_FROM_DEVICE:
            break;
        }

        app_default_msg_handler(msg);
    }

    app_loudspeaker_exit();

    return next_mode;
}




static int loudspeaker_mode_try_enter(int arg)
{
    return 0;
}

static int loudspeaker_mode_try_exit()
{

    // btstack_exit_in_other_mode();
    return 0;
}

static const struct app_mode_ops loudspeaker_mode_ops = {
    .try_enter      = loudspeaker_mode_try_enter,
    .try_exit       = loudspeaker_mode_try_exit,
};

/*
 * 注册 loudspeaker 模式
 */
REGISTER_APP_MODE(loudspeaker_mode) = {
    .name 	= APP_MODE_LOUDSPEAKER,
    .index  = APP_MODE_LOUDSPEAKER_INDEX,
    .ops 	= &loudspeaker_mode_ops,
};

static u8 loudspeaker_idle_query(void)
{
    return loudspeaker_idle_flag;
}
REGISTER_LP_TARGET(loudspeaker_lp_target) = {
    .name = "loudspeaker",
    .is_idle = loudspeaker_idle_query,
};


#endif



