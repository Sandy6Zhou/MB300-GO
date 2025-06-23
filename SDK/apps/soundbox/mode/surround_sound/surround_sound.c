#include "system/includes.h"
#include "app_action.h"
#include "app_main.h"
#include "default_event_handler.h"
#include "soundbox.h"
#include "tone_player.h"
#include "app_tone.h"
#include "surround_sound.h"
#include "ui/ui_api.h"
#include "ui_manage.h"
#include "local_tws.h"
#include "app_le_broadcast.h"
#include "app_le_connected.h"
#include "app_le_auracast.h"

#if TCFG_APP_SURROUND_SOUND_EN

static int surround_sound_tone_play_end_callback(void *priv, enum stream_event event)
{
    if (false == app_in_mode(APP_MODE_SURROUND_SOUND)) {
        return 0;
    }
    switch (event) {
    case STREAM_EVENT_NONE:
    case STREAM_EVENT_STOP:
        /* app_send_message(APP_MSG_LINEIN_START, 0); */
        break;
    default:
        break;
    }
    return 0;
}

static int app_surround_sound_init()
{
    int ret = -1;

    printf("app_surround sound init()!\n");

    if (ret != 0) {
        tone_player_stop();
        int ret = play_tone_file_callback(get_tone_files()->surround_sound_mode, NULL, surround_sound_tone_play_end_callback);
        if (ret) {
            surround_sound_tone_play_end_callback(NULL, STREAM_EVENT_NONE);
        }
    }
#if TCFG_PITCH_SPEED_NODE_ENABLE
    app_var.pitch_mode = PITCH_0;    //设置变调初始模式
#endif

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN)) || (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_CIS_CENTRAL_EN | LE_AUDIO_JL_CIS_PERIPHERAL_EN))
    btstack_init_in_other_mode();
#endif

    app_send_message(APP_MSG_ENTER_MODE, APP_MODE_SURROUND_SOUND);
    return 0;
}

void app_surround_sound_exit()
{
    /* app_surround_sound_stop(); */
    app_send_message(APP_MSG_EXIT_MODE, APP_MODE_SURROUND_SOUND);
    /* tone_player_stop(); */
}


struct app_mode *app_enter_surround_sound_mode(int arg)
{
    int msg[16];
    struct app_mode *next_mode;



    app_surround_sound_init();

#if (SURROUND_SOUND_FIX_ROLE_EN == 0)
    //非固定角色
    /* y_printf(">>>>>>>>>>>>> Surround Sound Role :%d\n", get_surround_sound_role()); */
#else
    y_printf(">>>>>>>>>>>>> Surround Sound Role :%d\n", SURROUND_SOUND_ROLE);
#endif


    while (1) {
        if (!app_get_message(msg, ARRAY_SIZE(msg), surround_sound_mode_key_table)) {
            continue;
        }
        next_mode = app_mode_switch_handler(msg);
        if (next_mode) {
            break;
        }

        switch (msg[0]) {
        case MSG_FROM_APP:
            surround_sound_app_msg_handler(msg + 1);
            break;
        case MSG_FROM_DEVICE:
            break;
        }

        app_default_msg_handler(msg);
    }

    app_surround_sound_exit();

    return next_mode;
}

static int surround_sound_mode_try_enter(int arg)
{
    //环绕声项目，只有接收端才能进入
#if SURROUND_SOUND_FIX_ROLE_EN
    //固定角色
    if (SURROUND_SOUND_ROLE != 0) {
        return 0;
    } else {
        r_printf("err, surround round role:%d, can't enter Surround Sound  mode\n", SURROUND_SOUND_ROLE);
        return -1;
    }
#else
    //不固定角色
    if (get_surround_sound_role() > SURROUND_SOUND_TX && get_surround_sound_role() < SURROUND_SOUND_ROLE_MAX) {
        return 0;
    } else {
        r_printf("err, surround round role:%d, can't enter Surround Sound mode\n", get_surround_sound_role());
        return -1;
    }
#endif
}

static int surround_sound_mode_try_exit()
{
    int ret = 0;
#if (LEA_BIG_CTRLER_TX_EN || LEA_BIG_CTRLER_RX_EN || LEA_CIG_CENTRAL_EN || LEA_CIG_PERIPHERAL_EN)
#if (LEA_BIG_CTRLER_TX_EN || LEA_BIG_CTRLER_RX_EN)
    le_audio_scene_deal(LE_AUDIO_APP_MODE_EXIT);
#if (!TCFG_BT_BACKGROUND_ENABLE && !TCFG_KBOX_1T3_MODE_EN)
    app_broadcast_close_in_other_mode();
#endif
#endif

#if (LEA_CIG_CENTRAL_EN || LEA_CIG_PERIPHERAL_EN)
    le_audio_scene_deal(LE_AUDIO_APP_MODE_EXIT);
#if (!TCFG_BT_BACKGROUND_ENABLE && !TCFG_KBOX_1T3_MODE_EN)
    app_connected_close_in_other_mode();
#endif
#endif

#if (!TCFG_KBOX_1T3_MODE_EN)
    ret = btstack_exit_in_other_mode();
#endif
#endif

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN))
    app_broadcast_deal(LE_AUDIO_APP_MODE_EXIT);
#if (!TCFG_BT_BACKGROUND_ENABLE)
    ret = btstack_exit_in_other_mode();
#endif
#endif

    return ret;
}

static const struct app_mode_ops surround_sound_mode_ops = {
    .try_enter      = surround_sound_mode_try_enter,
    .try_exit       = surround_sound_mode_try_exit,
};

/*
 * 注册 surround_sound 模式
 */
REGISTER_APP_MODE(surround_sound_mode) = {
    .name 	= APP_MODE_SURROUND_SOUND,
    .index  = APP_MODE_SURROUND_SOUND_INDEX,
    .ops 	= &surround_sound_mode_ops,
};

static u8 surround_sound_idle_query(void)
{
    return 0;
}
REGISTER_LP_TARGET(surround_sound_lp_target) = {
    .name = "surround_sound",
    .is_idle = surround_sound_idle_query,
};

#endif
