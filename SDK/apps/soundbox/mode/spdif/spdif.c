
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".spdif.data.bss")
#pragma data_seg(".spdif.data")
#pragma const_seg(".spdif.text.const")
#pragma code_seg(".spdif.text")
#endif
#include "app_main.h"
#if TCFG_APP_SPDIF_EN
#include "system/includes.h"
#include "app_action.h"
#include "default_event_handler.h"
#include "soundbox.h"
#include "spdif.h"
#include "app_tone.h"
#include "spdif_file.h"
#include "spdif_player.h"
#include "audio_dai/audio_spdif_slave.h"
#include "hdmi_cec_api.h"
#include "gpio_config.h"
#include "audio_config.h"
#include "media/includes.h"
#include "wireless_trans.h"
#include "le_audio_recorder.h"
#include "le_broadcast.h"
#include "app_le_broadcast.h"
#include "le_connected.h"
#include "app_le_connected.h"
#include "le_audio_stream.h"
#include "le_audio_player.h"
#include "local_tws.h"
#include "rcsp_spdif_func.h"
#include "bt_key_func.h"
#include "btstack_rcsp_user.h"
#include "app_le_auracast.h"

struct spdif_ctl {
    struct spdif_file_cfg *p_spdif_cfg;	//spdif的配置参数信息
    volatile u8 mute_mark;		//这里的mute是指spdif 软件mute，非系统mute, 只作用于spdif
    /* u16 le_audio_role_detect_timeout_id;	//timeout定时器id，用来做广播发送、接收角色的切换 */
    struct le_audio_stream_params params;
    void *le_audio;
};
struct spdif_ctl app_spdif_hd;
static u8 spdif_idle_flag = 1;

static int le_audio_spdif_volume_pp(void);
/* 模式提示音播放回调 */
static int spdif_tone_play_end_callback(void *priv, enum stream_event event)
{
    if (false == app_in_mode(APP_MODE_SPDIF)) {
        return 0;
    }
    switch (event) {
    /* case STREAM_EVENT_NONE: */
    case STREAM_EVENT_STOP:
        app_send_message(APP_MSG_SPDIF_START, 0);
        break;
    default:
        break;
    }
    return 0;
}

u8 get_spdif_mute_state(void)
{
    return app_spdif_hd.mute_mark;
}

int spdif_app_msg_handler(int *msg)
{
    if (false == app_in_mode(APP_MODE_SPDIF)) {
        return 0;
    }
    switch (msg[0]) {
    case APP_MSG_CHANGE_MODE:
        app_send_message(APP_MSG_GOTO_NEXT_MODE, 0);
        break;
    case APP_MSG_MUSIC_PP:
        puts("APP_MSG_MUSIC_PP\n");
        app_spdif_hd.mute_mark = spdif_get_data_clean_flag();
        app_spdif_hd.mute_mark ^= 1;
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN)) || (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SOURCE_EN | LE_AUDIO_AURACAST_SINK_EN))
#if (LEA_BIG_FIX_ROLE==0)
        if (!get_le_audio_curr_role()) {
            // 非广播
        } else {
            if (get_le_audio_curr_role() == 2) {
                //如果此时是接收端, 则需要转为发送端
                app_spdif_hd.mute_mark = 0;
            } else {
                //如果此时是发送端, 则需要转为接收端
                app_spdif_hd.mute_mark = 1;
            }
        }
#endif
#endif
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN)) || (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SOURCE_EN | LE_AUDIO_AURACAST_SINK_EN))
        if (get_le_audio_curr_role()) {
            le_audio_spdif_volume_pp();
        }
        if (app_spdif_hd.mute_mark == 0 && !get_le_audio_curr_role()) {
            //非mute状态并且非广播
            spdif_stream_start();
        }
#endif
        spdif_set_data_clean(app_spdif_hd.mute_mark);
        break;
    case APP_MSG_MUSIC_NEXT:
        break;
    case APP_MSG_MUSIC_PREV:
        break;
    case APP_MSG_SPDIF_START:
        if (le_audio_scene_deal(LE_AUDIO_APP_MODE_ENTER) > 0) {
            /* break; */
        }

        if ((uuid2gpio(app_spdif_hd.p_spdif_cfg->cec_io_port) != 0xff) && app_spdif_hd.p_spdif_cfg->hdmi_det_mode == HDMI_DET_UNUSED) {
            hdmi_cec_init(uuid2gpio(app_spdif_hd.p_spdif_cfg->cec_io_port), 0);
        }

        spdif_init();
        spdif_start();
        spdif_stream_start();
        break;
    case APP_MSG_SPDIF_SWITCH_SOURCE:
        spdif_io_loop_switch();
        printf("spdif switch source");
        break;
    case APP_MSG_SPDIF_SET_SOURCE:
        spdif_set_port_by_index(msg[1]);
        app_send_message(APP_MSG_SPDIF_SOURCE_UPDATE, 0);
        //printf("spdif set source %d\n",msg[1]);
        break;
    case APP_MSG_CEC_VOL_UP:
        if (uuid2gpio(app_spdif_hd.p_spdif_cfg->hdmi_port[0]) != get_spdif_source_io() &&
            uuid2gpio(app_spdif_hd.p_spdif_cfg->hdmi_port[1]) != get_spdif_source_io()) {
            //当前播放源不是HDMI ARC
            break;
        }
    /* fall-through */
    case APP_MSG_VOL_UP:
#if (THIRD_PARTY_PROTOCOLS_SEL & (RCSP_MODE_EN))
        if (bt_rcsp_device_conn_num() && JL_rcsp_get_auth_flag() && (app_get_current_mode()->name != APP_MODE_BT)) {
            bt_key_rcsp_vol_up();
        } else {
            app_audio_volume_up(1);
        }
#else
        app_audio_volume_up(1);
#endif
        if (app_audio_get_volume(APP_AUDIO_CURRENT_STATE) == app_audio_get_max_volume()) {
            if (tone_player_runing() == 0) {
#if TCFG_MAX_VOL_PROMPT
                play_tone_file(get_tone_files()->max_vol);
#endif
            }
        }
        if (uuid2gpio(app_spdif_hd.p_spdif_cfg->cec_io_port) != 0xff) {
            if (uuid2gpio(app_spdif_hd.p_spdif_cfg->hdmi_port[0]) == get_spdif_source_io() ||
                uuid2gpio(app_spdif_hd.p_spdif_cfg->hdmi_port[1]) == get_spdif_source_io()) {
                //当前播放源为HDMI ARC
                hdmi_cec_send_volume(app_audio_get_volume(APP_AUDIO_STATE_MUSIC));
            }
        }
        app_send_message(APP_MSG_VOL_CHANGED, app_audio_get_volume(APP_AUDIO_STATE_MUSIC));
        break;
    case APP_MSG_CEC_VOL_DOWN:
        if (uuid2gpio(app_spdif_hd.p_spdif_cfg->hdmi_port[0]) != get_spdif_source_io() &&
            uuid2gpio(app_spdif_hd.p_spdif_cfg->hdmi_port[1]) != get_spdif_source_io()) {
            //当前播放源不是HDMI ARC
            break;
        }
    /* fall-through */
    case APP_MSG_VOL_DOWN:
#if (THIRD_PARTY_PROTOCOLS_SEL & (RCSP_MODE_EN))
        if (bt_rcsp_device_conn_num() && JL_rcsp_get_auth_flag() && (app_get_current_mode()->name != APP_MODE_BT)) {
            bt_key_rcsp_vol_down();
        } else {
            app_audio_volume_down(1);
        }
#else
        app_audio_volume_down(1);
#endif
        if (uuid2gpio(app_spdif_hd.p_spdif_cfg->cec_io_port) != 0xff) {
            if (uuid2gpio(app_spdif_hd.p_spdif_cfg->hdmi_port[0]) == get_spdif_source_io() ||
                uuid2gpio(app_spdif_hd.p_spdif_cfg->hdmi_port[1]) == get_spdif_source_io()) {
                //当前播放源为HDMI ARC

                hdmi_cec_send_volume(app_audio_get_volume(APP_AUDIO_STATE_MUSIC));
            }
        }
        app_send_message(APP_MSG_VOL_CHANGED, app_audio_get_volume(APP_AUDIO_STATE_MUSIC));
        break;
    case APP_MSG_PITCH_UP:
#if TCFG_PITCH_SPEED_NODE_ENABLE
        printf("app msg spdif pitch up\n");
        if (spdif_player_runing()) {
            app_var.pitch_mode = spdif_file_pitch_up(); //返回当前变调模式
        }
#endif
        break;
    case APP_MSG_PITCH_DOWN:
#if TCFG_PITCH_SPEED_NODE_ENABLE
        printf("app msg spdif pitch down\n");
        if (spdif_player_runing()) {
            app_var.pitch_mode = spdif_file_pitch_down();
        }
#endif
        break;
    case APP_MSG_CEC_MUTE:
        if (uuid2gpio(app_spdif_hd.p_spdif_cfg->hdmi_port[0]) == get_spdif_source_io() ||
            uuid2gpio(app_spdif_hd.p_spdif_cfg->hdmi_port[1]) == get_spdif_source_io()) {
            //当前播放源为HDMI ARC
            u8 mute_ctl = msg[1];
            printf("\n CEC_MUTE CTL %d \n", mute_ctl);
            if (mute_ctl > 1) {
#if TCFG_DAC_NODE_ENABLE
                if (get_sys_aduio_mute_statu()) {
                    app_audio_mute(AUDIO_UNMUTE_DEFAULT);
                } else {
                    app_audio_mute(AUDIO_MUTE_DEFAULT);
                }
#endif
                hdmi_cec_send_volume(app_audio_get_volume(APP_AUDIO_STATE_MUSIC));
            } else {
#if TCFG_DAC_NODE_ENABLE
                if (mute_ctl) {
                    app_audio_mute(AUDIO_MUTE_DEFAULT);
                } else {
                    app_audio_mute(AUDIO_UNMUTE_DEFAULT);
                }
#endif
            }

            app_send_message(APP_MSG_MUTE_CHANGED, get_sys_aduio_mute_statu());
        }
        break;
    case APP_MSG_SYS_MUTE:
#if TCFG_DAC_NODE_ENABLE
        u8 sys_audio_mute_statu = app_audio_get_dac_digital_mute() ^ 1;
        if (sys_audio_mute_statu) {
            app_audio_mute(AUDIO_MUTE_DEFAULT);
        } else {
            app_audio_mute(AUDIO_UNMUTE_DEFAULT);
        }
        update_spdif_player_mute_state();
        app_send_message(APP_MSG_MUTE_CHANGED, sys_audio_mute_statu);
#endif
        break;
    case APP_MSG_SPDIF_STREAM_RUN:
        y_printf(">>>>>>>>>>>>>>> APP_MSG_SPDIF_STREAM_RUN!");
        spdif_stream_run_open_player();
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN)) || (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SOURCE_EN | LE_AUDIO_AURACAST_SINK_EN))
        if (get_le_audio_curr_role() != 2) {
            spdif_open_player();
        } else if (get_le_audio_curr_role() == 2 && !spdif_get_data_clean_flag()) {
#if (LEA_BIG_FIX_ROLE == 0)
            spdif_open_le_audio();
#endif
            spdif_open_player();
        }
#else
        spdif_open_player();
#endif
        break;
    default:
        app_common_key_msg_handler(msg);
        break;
    }

#if RCSP_MODE
    rcsp_spdif_msg_deal(msg[0]);
#endif

    return 0;
}

void spdif_local_start(void *priv)
{
    app_send_message(APP_MSG_SPDIF_START, 0);
}

static int app_spdif_init()
{
    int ret = -1;
    puts("\nspdif start\n");
    spdif_first_in_flag = 1;
    app_spdif_hd.mute_mark = 0;
    spdif_set_data_clean(app_spdif_hd.mute_mark);	//默认每次进spdif都是非mute状态(自身mute)
    spdif_idle_flag = 0;
    /* app_spdif_hd.le_audio_role_detect_timeout_id = 0; */
#if TCFG_LOCAL_TWS_ENABLE
    ret = local_tws_enter_mode(get_tone_files()->spdif_mode, NULL);
    if (ret != 0) {
        tone_player_stop();
        ret = play_tone_file_callback(get_tone_files()->spdif_mode, NULL, spdif_tone_play_end_callback);
        if (ret) {
            spdif_tone_play_end_callback(NULL, STREAM_EVENT_NONE); // 提示音播放失败就直接调用 linein start
        }
    }
#else //TCFG_LOCAL_TWS_ENABLE
    //开启ui
    /* UI_SHOW_WINDOW(ID_WINDOW_TV);//打开ui主页 */
    /* UI_SHOW_MENU(MENU_TV, 0, 0, NULL); */
    tone_player_stop();
    ret = play_tone_file_callback(get_tone_files()->spdif_mode, NULL, spdif_tone_play_end_callback);
    if (ret) {
        spdif_tone_play_end_callback(NULL, STREAM_EVENT_NONE); // 提示音播放失败就直接调用 linein start
    }
#endif
#if TCFG_PITCH_SPEED_NODE_ENABLE
    app_var.pitch_mode = PITCH_0;
#endif

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SOURCE_EN | LE_AUDIO_AURACAST_SINK_EN)) || \
    (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_UNICAST_SOURCE_EN | LE_AUDIO_UNICAST_SINK_EN)) || \
    (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN)) || \
    (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_CIS_CENTRAL_EN | LE_AUDIO_JL_CIS_PERIPHERAL_EN))
    btstack_init_in_other_mode();
#endif

    app_send_message(APP_MSG_ENTER_MODE, APP_MODE_SPDIF);
    app_spdif_hd.p_spdif_cfg = audio_spdif_file_get_cfg();
    app_send_message(APP_MSG_SPDIF_SOURCE_UPDATE, 0);
    app_send_message(APP_MSG_SPDIF_STATUS_UPDATE, 0);
    return 0;
}

void app_spdif_exit()
{
    printf("--- app_spdif_exit ---\n");
#if TCFG_LOCAL_TWS_ENABLE
    local_tws_exit_mode();
#endif
    if (app_spdif_hd.p_spdif_cfg->hdmi_det_mode == HDMI_DET_UNUSED \
        && (uuid2gpio(app_spdif_hd.p_spdif_cfg->cec_io_port) != 0xff)) {
        hdmi_cec_close();
    }

    if (spdif_player_runing()) {
        spdif_player_close();
    } else {
    }
    spdif_stop();
    spdif_release();
    spdif_idle_flag = 1;
    app_send_message(APP_MSG_EXIT_MODE, APP_MODE_SPDIF);
}

struct app_mode *app_enter_spdif_mode(int arg)
{
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SOURCE_EN | LE_AUDIO_AURACAST_SINK_EN))
    int msg[32];
#else
    int msg[16];
#endif
    struct app_mode *next_mode;

    app_spdif_init();

    while (1) {
        if (!app_get_message(msg, ARRAY_SIZE(msg), spdif_mode_key_table)) {
            continue;
        }
        next_mode = app_mode_switch_handler(msg);
        if (next_mode) {
            break;
        }

        switch (msg[0]) {
        case MSG_FROM_APP:
            spdif_app_msg_handler(msg + 1);
            break;
        case MSG_FROM_DEVICE:
            break;
        }

        app_default_msg_handler(msg);
    }

    app_spdif_exit();

    return next_mode;
}

static int spdif_mode_try_enter()
{
    /* return app_spdif_init(); */
    return 0;
}

static int spdif_mode_try_exit()
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

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SINK_EN | LE_AUDIO_AURACAST_SOURCE_EN))
    le_audio_scene_deal(LE_AUDIO_APP_MODE_EXIT);
#if (!TCFG_BT_BACKGROUND_ENABLE)
    app_auracast_close_in_other_mode();
    ret = btstack_exit_in_other_mode();
#endif
#endif

    /* app_spdif_exit(); */
    return ret;
}

static const struct app_mode_ops spdif_mode_ops = {
    .try_enter          = spdif_mode_try_enter,
    .try_exit           = spdif_mode_try_exit,
};

/*
 * 注册spdif模式
 */
REGISTER_APP_MODE(spdif_mode) = {
    .name 	= APP_MODE_SPDIF,
    .index  = APP_MODE_SPDIF_INDEX,
    .ops 	= &spdif_mode_ops,
};

REGISTER_LOCAL_TWS_OPS(spdif) = {
    .name 	= APP_MODE_SPDIF,
    .local_audio_open = spdif_local_start,
    .get_play_status = spdif_player_runing,
};

static u8 spdif_idle_query(void)
{
    return spdif_idle_flag;
}

REGISTER_LP_TARGET(spdif_lp_target) = {
    .name = "spdif",
    .is_idle = spdif_idle_query,
};

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SOURCE_EN | LE_AUDIO_AURACAST_SINK_EN)) || \
    (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_UNICAST_SOURCE_EN | LE_AUDIO_UNICAST_SINK_EN)) || \
    (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN)) || \
    (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_CIS_CENTRAL_EN | LE_AUDIO_JL_CIS_PERIPHERAL_EN))
static int get_spdif_play_status(void)
{
    if ((get_le_audio_app_mode_exit_flag() || app_spdif_hd.mute_mark)) {
        y_printf("le_audio spdif get play status: Stop!\n");
        if (get_spdif_driver_state() == AUDIO_SPDIF_STATE_START) {
            //如果驱动是打开的，则要关闭驱动
        }
        return LOCAL_AUDIO_PLAYER_STATUS_STOP;
    }
    y_printf("le_audio spdif get play status: Play!\n");
    return LOCAL_AUDIO_PLAYER_STATUS_PLAY;
}

static int spdif_local_audio_open(void)
{
    y_printf(">> Enter spdif_local_audio_open!\n");
    if (1) {//(get_iis_play_status() == LOCAL_AUDIO_PLAYER_STATUS_PLAY) {
        //打开本地播放
        if ((uuid2gpio(app_spdif_hd.p_spdif_cfg->cec_io_port) != 0xff) && app_spdif_hd.p_spdif_cfg->hdmi_det_mode == HDMI_DET_UNUSED) {
            hdmi_cec_init(uuid2gpio(app_spdif_hd.p_spdif_cfg->cec_io_port), 0);
        }
    }
    spdif_stream_start();

    return 0;
}


static int spdif_local_audio_close(void)
{
    y_printf(">>>>>>>>>>>>>>> spdif_local_audio_close!\n");
    spdif_stream_stop();

    if (get_spdif_play_status() == LOCAL_AUDIO_PLAYER_STATUS_PLAY) {
        if (spdif_player_runing()) {
            //关闭本地播放
            spdif_player_close();
        }
    }
    return 0;
}

static void *spdif_tx_le_audio_open(void *args)
{
    int err;
    void *le_audio = NULL;
    y_printf(">>>>>>>>>>>>>>> spdif_tx_le_audio_open!\n");
    if (1) {//(get_iis_play_status() == LOCAL_AUDIO_PLAYER_STATUS_PLAY) {
        if ((uuid2gpio(app_spdif_hd.p_spdif_cfg->cec_io_port) != 0xff) && app_spdif_hd.p_spdif_cfg->hdmi_det_mode == HDMI_DET_UNUSED) {
            hdmi_cec_init(uuid2gpio(app_spdif_hd.p_spdif_cfg->cec_io_port), 0);
        }
        //打开广播音频播放
        struct le_audio_stream_params *params = (struct le_audio_stream_params *)args;
        le_audio = le_audio_stream_create(params->conn, &params->fmt);
        struct le_audio_stream_format *le_audio_fmt = &params->fmt;
        memcpy(&app_spdif_hd.params, params, sizeof(struct le_audio_stream_params));
        app_spdif_hd.le_audio = le_audio;
    }
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN))
    //修复spdif模式下反复开关广播后，在广播下按下pp键，本地mute住但接收端依旧出声的问题
    update_app_broadcast_deal_scene(LE_AUDIO_MUSIC_START);
#endif

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SOURCE_EN | LE_AUDIO_AURACAST_SINK_EN))
    //修复spdif模式下反复开关广播后，在广播下按下pp键，本地mute住但接收端依旧出声的问题
    update_app_auracast_deal_scene(LE_AUDIO_MUSIC_START);
#endif
    spdif_stream_start();


    return le_audio;
}

static int spdif_tx_le_audio_close(void *le_audio)
{
    y_printf("spdif tx le close!\n");
    if (!le_audio) {
        return -EPERM;
    }
    spdif_stream_stop();
    //关闭广播音频播放
#if LEA_LOCAL_SYNC_PLAY_EN
    le_audio_player_close(le_audio);
#endif
    le_audio_spdif_recorder_close();
    le_audio_stream_free(le_audio);
    app_spdif_hd.le_audio = NULL;
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN))
    update_app_broadcast_deal_scene(LE_AUDIO_MUSIC_STOP);
#endif
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SOURCE_EN | LE_AUDIO_AURACAST_SINK_EN))
    update_app_auracast_deal_scene(LE_AUDIO_MUSIC_STOP);
#endif
    /* app_audio_set_volume(APP_AUDIO_STATE_MUSIC, __this->volume, 1); */
    return 0;
}

static int spdif_rx_le_audio_open(void *rx_audio, void *args)
{
    int err;
    struct le_audio_player_hdl *rx_audio_hdl = (struct le_audio_player_hdl *)rx_audio;
    if (!rx_audio) {
        return -EPERM;
    }
    //打开广播音频播放
    struct le_audio_stream_params *params = (struct le_audio_stream_params *)args;
    rx_audio_hdl->le_audio = le_audio_stream_create(params->conn, &params->fmt);
    rx_audio_hdl->rx_stream = le_audio_stream_rx_open(rx_audio_hdl->le_audio, params->fmt.coding_type);
    err = le_audio_player_open(rx_audio_hdl->le_audio, params);
    if (err != 0) {
        ASSERT(0, "player open fail");
    }

    return 0;
}

static int spdif_rx_le_audio_close(void *rx_audio)
{
    struct le_audio_player_hdl *rx_audio_hdl = (struct le_audio_player_hdl *)rx_audio;
    if (!rx_audio) {
        return -EPERM;
    }
    //关闭广播音频播放
    le_audio_player_close(rx_audio_hdl->le_audio);
    le_audio_stream_rx_close(rx_audio_hdl->rx_stream);
    le_audio_stream_free(rx_audio_hdl->le_audio);
    return 0;
}

static int le_audio_spdif_volume_pp(void)
{
    int ret = 0;
    if (!app_spdif_hd.mute_mark) {
        //mute_mark = 0;
        y_printf(">>>>>>>>>>> %s, le_audio_scene_deal(LE_AUDIO_MUSIC_START)\n", __func__);
        ret = le_audio_scene_deal(LE_AUDIO_MUSIC_START);
    } else {
        //mute_mark = 1;
        y_printf(">>>>>>>>>>> %s, le_audio_scene_deal(LE_AUDIO_MUSIC_STOP)\n", __func__);
        ret = le_audio_scene_deal(LE_AUDIO_MUSIC_STOP);
    }
    return ret;
}

const struct le_audio_mode_ops le_audio_spdif_ops = {
    .local_audio_open = spdif_local_audio_open,
    .local_audio_close = spdif_local_audio_close,
    .tx_le_audio_open = spdif_tx_le_audio_open,
    .tx_le_audio_close = spdif_tx_le_audio_close,
    .rx_le_audio_open = spdif_rx_le_audio_open,
    .rx_le_audio_close = spdif_rx_le_audio_close,
    .play_status = get_spdif_play_status,
};



struct le_audio_stream_params *spdif_get_le_audio_params(void)
{
    return &app_spdif_hd.params;
}


void *spdif_get_le_audio_hdl(void)
{
    return app_spdif_hd.le_audio;
}



#endif
#endif

