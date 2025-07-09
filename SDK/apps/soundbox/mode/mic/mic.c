
#include "app_main.h"
#include "system/includes.h"
#include "app_action.h"
#include "default_event_handler.h"
#include "soundbox.h"
#include "app_tone.h"
#include "gpio_config.h"
#include "audio_config.h"
#include "media/includes.h"
#include "adc_file.h"
#include "mic_player.h"
#include "mic.h"
#include "app_le_broadcast.h"
#include "app_le_connected.h"
#include "mic_effect.h"
#include "app_le_auracast.h"
#include "local_tws.h"

#if TCFG_APP_MIC_EN

static u8 mic_idle_flag = 1;

#if TCFG_MIC_EFFECT_ENABLE
// 混响开关策略：mic模式下和混响不能同时存在
static u8 mic_effect_open_flag = 0;		//记录混响开关状态
#endif


static int mic_tone_play_end_callback(void *priv, enum stream_event event)
{
    if (false == app_in_mode(APP_MODE_MIC)) {
        return 0;
    }
    switch (event) {
    case STREAM_EVENT_NONE:
    case STREAM_EVENT_STOP:
        app_send_message(APP_MSG_MIC_START, 0);
        break;
    default:
        break;
    }
    return 0;
}

void app_mic_exit()
{
#if TCFG_LOCAL_TWS_ENABLE
    local_tws_exit_mode();
#endif
    app_set_current_mode(app_get_mode_by_name(APP_MODE_NULL));        //这里把MODE设置成NULL，防止快速切模式的时候提示音还没播完已经退出当前模式，在提示音回调里还继续开广播

    mic_stop();
    mic_idle_flag = 1;
    app_send_message(APP_MSG_EXIT_MODE, APP_MODE_MIC);
    /* tone_player_stop(); */
#if TCFG_MIC_EFFECT_ENABLE
    if (mic_effect_open_flag == 1) {
        //如果切进来mic模式前开着混响，则切出mic模式时还原混响的打开
        mic_effect_open_flag = 0;
        mic_effect_player_open();
    }
#endif
}

static int app_mic_init(void)
{
    int ret = -1;
    mic_idle_flag = 0;
#if TCFG_MIC_EFFECT_ENABLE
    //如果混响 打开着，那么关闭混响, 记录下混响状态
    if (mic_effect_player_runing()) {
        mic_effect_open_flag = 1;
        mic_effect_player_close();
    } else {
        mic_effect_open_flag = 0;
    }
#endif

    tone_player_stop();

#if TCFG_LOCAL_TWS_ENABLE
    ret = local_tws_enter_mode(get_tone_files()->mic_mode, NULL);
#endif //TCFG_LOCAL_TWS_ENABLE

    if (ret != 0) {
        ret = play_tone_file_callback(get_tone_files()->mic_mode, NULL, mic_tone_play_end_callback);
        if (ret) {
            //提示音播放失败
            mic_tone_play_end_callback(NULL, STREAM_EVENT_NONE);
        }
    }

#ifdef TCFG_PITCH_SPEED_NODE_ENABLE
    app_var.pitch_mode = PITCH_0;    //设置变调初始模式
#endif

#if (LEA_BIG_CTRLER_TX_EN || LEA_BIG_CTRLER_RX_EN || LEA_CIG_CENTRAL_EN || LEA_CIG_PERIPHERAL_EN)
    btstack_init_in_other_mode();
#if (LEA_BIG_FIX_ROLE == LEA_ROLE_AS_RX)
    //如果固定为接收端，则会存在的问题：打开广播下从其它模式切进mic模式，关闭广播mic不会自动打开的问题
    mic_set_local_open_flag(1);
    printf(">>>>> Call Func: mic_set_local_open_flag(1), In Func:%s, %d\n", __func__, __LINE__);
#endif
#endif

    app_send_message(APP_MSG_ENTER_MODE, APP_MODE_MIC);
    return 0;
}

struct app_mode *app_enter_mic_mode(int arg)
{
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SOURCE_EN | LE_AUDIO_AURACAST_SINK_EN))
    int msg[32];
#else
    int msg[16];
#endif
    struct app_mode *next_mode;

    app_mic_init();

    while (1) {
        if (!app_get_message(msg, ARRAY_SIZE(msg), mic_mode_key_table)) {
            continue;
        }
        next_mode = app_mode_switch_handler(msg);
        if (next_mode) {
            break;
        }

        switch (msg[0]) {
        case MSG_FROM_APP:
            mic_app_msg_handler(msg + 1);
            break;
        case MSG_FROM_DEVICE:
            break;
        }

        app_default_msg_handler(msg);
    }

    app_mic_exit();

    return next_mode;
}


static int mic_mode_try_enter(int arg)
{
    return 0;
}

static int mic_mode_try_exit()
{
    int ret = 0;
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN)) || \
    (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_CIS_CENTRAL_EN | LE_AUDIO_JL_CIS_PERIPHERAL_EN))
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN))
    le_audio_scene_deal(LE_AUDIO_APP_MODE_EXIT);
#if (!TCFG_BT_BACKGROUND_ENABLE && !TCFG_KBOX_1T3_MODE_EN)
    app_broadcast_close_in_other_mode();
#endif
#endif

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_CIS_CENTRAL_EN | LE_AUDIO_JL_CIS_PERIPHERAL_EN))
    le_audio_scene_deal(LE_AUDIO_APP_MODE_EXIT);
#if (!TCFG_BT_BACKGROUND_ENABLE && !TCFG_KBOX_1T3_MODE_EN)
    app_connected_close_in_other_mode();
#endif
#endif

#if (!TCFG_KBOX_1T3_MODE_EN)
    ret = btstack_exit_in_other_mode();
#endif
#endif

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SOURCE_EN | LE_AUDIO_AURACAST_SINK_EN))
    le_audio_scene_deal(LE_AUDIO_APP_MODE_EXIT);
#if (!TCFG_BT_BACKGROUND_ENABLE)
    app_auracast_close_in_other_mode();
    ret = btstack_exit_in_other_mode();
#endif
#endif
    return ret;
}

static const struct app_mode_ops mic_mode_ops = {
    .try_enter      = mic_mode_try_enter,
    .try_exit       = mic_mode_try_exit,
};

/*
 * 注册 mic 模式
 */
REGISTER_APP_MODE(mic_mode) = {
    .name 	= APP_MODE_MIC,
    .index  = APP_MODE_MIC_INDEX,
    .ops 	= &mic_mode_ops,
};

static u8 mic_idle_query(void)
{
    return mic_idle_flag;
}
REGISTER_LP_TARGET(mic_lp_target) = {
    .name = "mic",
    .is_idle = mic_idle_query,
};

void mic_local_start(void *priv)
{
    mic_start();
}

static bool get_mic_player_status(void)
{
    return mic_get_status();
}

REGISTER_LOCAL_TWS_OPS(mic) = {
    .name 	= APP_MODE_MIC,
    .local_audio_open = mic_local_start,
    .get_play_status = get_mic_player_status,
};




#endif

