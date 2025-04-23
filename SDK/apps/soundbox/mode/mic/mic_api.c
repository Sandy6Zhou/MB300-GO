#include "system/includes.h"
#include "app_config.h"
#include "app_task.h"
#include "media/includes.h"
#include "tone_player.h"
#include "app_tone.h"
#include "app_main.h"
#include "ui_manage.h"
#include "vm.h"
#include "ui/ui_api.h"
#include "scene_switch.h"
#include "audio_config_def.h"
#include "audio_config.h"
#include "mic_player.h"
#include "wireless_trans.h"
#include "le_audio_recorder.h"
#include "le_broadcast.h"
#include "app_le_broadcast.h"
#include "le_connected.h"
#include "app_le_connected.h"
#include "le_audio_stream.h"
#include "le_audio_player.h"
#include "app_le_auracast.h"




#if TCFG_APP_MIC_EN

#define LOG_TAG             "[APP_MIC]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
/* #define LOG_DUMP_ENABLE */
#define LOG_CLI_ENABLE
#include "debug.h"

struct mic_opr {
    s16 volume ;
    u8 onoff;
    //以下两个变量都是为了解决问题：固定为接收端时，关闭广播恢复播放状态的问题
    u8 onoff_as_broadcast_receive;	//在接收模式下记录的mic状态
    u8 last_run_local_audio_close;	//上一次是否有跑 local_audio_close 函数
    //如果固定为接收端，则会存在的问题：打开广播下从其它模式切进mic模式，关闭广播mic不会自动打开的问题
    u8 mic_local_need_open_flag;	//当此flag置1时，需要打开mic
    u8 audio_state; /*判断mic模式使用模拟音量还是数字音量*/
    //暂停中开广播再关闭：暂停。暂停中开广播点击pp后关闭：播放。播歌开广播点击pp后关闭广播：暂停. 该变量为1时表示关闭广播时需要本地音频需要是播放状态
    u8 mic_local_audio_resume_onoff;
};
static struct mic_opr mic_hdl = {0};
#define __this 	(&mic_hdl)

static int le_audio_mic_volume_pp(void);


// 打开mic数据流操作
int mic_start(void)
{
    if (__this->onoff == 1) {
        printf("mic is aleady start\n");
        return true;
    }
    mic_player_open();
    __this->audio_state = APP_AUDIO_STATE_MUSIC;
    __this->volume = app_audio_get_volume(__this->audio_state);
    __this->onoff = 1;
#if ((LEA_BIG_CTRLER_TX_EN || LEA_BIG_CTRLER_RX_EN) && (LEA_BIG_FIX_ROLE == LEA_ROLE_AS_RX))
    if (__this->mic_local_need_open_flag) {
        __this->mic_local_need_open_flag = 0;
    }
#endif
    return true;
}


// stop mic 数据流
void mic_stop(void)
{
    if (__this->onoff == 0) {
        printf("mic is aleady stop\n");
        return;
    }
    mic_player_close();

    app_audio_set_volume(APP_AUDIO_STATE_MUSIC, __this->volume, 1);
    __this->onoff = 0;
}

//当固定为接收端时，其它模式下开广播切进mic模式，关闭广播后music模式不会自动播放
void mic_set_local_open_flag(u8 en)
{
    __this->mic_local_need_open_flag = en;
}


/*-------------------------------------------------------------------*/
/**@brief    mic 数据流 start stop 播放暂停切换
  @param    无
  @return   无
  @note
 */
/*-------------------------------------------------------------------*/
int mic_volume_pp(void)
{
    int ret = 0;
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN))
#if (LEA_BIG_FIX_ROLE == LEA_ROLE_AS_RX)
    //固定为接收端
    u8 mic_volume_mute_mark = app_audio_get_mute_state(APP_AUDIO_STATE_MUSIC);
    if (get_broadcast_role() == 2) {
        //接收端已连上
        mic_volume_mute_mark ^= 1;
        audio_app_mute_en(mic_volume_mute_mark);
        return 0;
    } else {
        if (mic_volume_mute_mark == 1) {
            //没有连接情况下，如果之前是mute住了，那么先解mute
            mic_volume_mute_mark ^= 1;
            audio_app_mute_en(mic_volume_mute_mark);
            return 0;
        }
        if (__this->onoff) {
            update_app_broadcast_deal_scene(LE_AUDIO_MUSIC_STOP);
        } else {
            update_app_broadcast_deal_scene(LE_AUDIO_MUSIC_START);
        }
    }
#else
    if (get_broadcast_role()) {
        ret = le_audio_mic_volume_pp();
    } else {
        if (__this->onoff) {
            update_app_broadcast_deal_scene(LE_AUDIO_MUSIC_STOP);
        } else {
            update_app_broadcast_deal_scene(LE_AUDIO_MUSIC_START);
        }
    }
#endif
#endif

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_CIS_CENTRAL_EN | LE_AUDIO_JL_CIS_PERIPHERAL_EN))
    if ((app_get_connected_role() == APP_CONNECTED_ROLE_TRANSMITTER) ||
        (app_get_connected_role() == APP_CONNECTED_ROLE_DUPLEX)) {
        ret = le_audio_mic_volume_pp();
    } else {
        if (__this->onoff) {
            update_app_connected_deal_scene(LE_AUDIO_MUSIC_STOP);
        } else {
            update_app_connected_deal_scene(LE_AUDIO_MUSIC_START);
        }
    }
#endif

    if (ret <= 0) {
        if (__this->onoff) {
            mic_stop();
        } else {
            mic_start();
        }
    }

    y_printf(">>>>>>>>>>>>>>>>> [%s] pp:%d \n", __func__, __this->onoff);
    /* UI_REFLASH_WINDOW(true); */
    return  __this->onoff;
}

/*-------------------------------------------------------------------*/
/**@brief   获取 mic 播放状态
  @param    无
  @return   1:当前正在打开 0：当前正在关闭
  @note
 */
/*-------------------------------------------------------------------*/
u8 mic_get_status(void)
{
    return __this->onoff;
}

/*-------------------------------------------------------------------*/
/*@brief   mic 音量设置函数
  @param    需要设置的音量
  @return
  @note    在mic 情况下针对了某些情景进行了处理，设置音量需要使用独立接口
*/
/*-------------------------------------------------------------------*/
int mic_volume_set(s16 vol)
{
    app_audio_set_volume(__this->audio_state, vol, 1);
    printf("mic vol: %d", __this->volume);
    __this->volume = vol;

#if (TCFG_DEC2TWS_ENABLE)
    bt_tws_sync_volume();
#endif
    return true;
}

void mic_key_vol_up(void)
{
    s16 vol;
    if (__this->volume < app_audio_volume_max_query(AppVol_MIC)) {
        __this->volume ++;
        mic_volume_set(__this->volume);
    } else {
        mic_volume_set(__this->volume);
        if (tone_player_runing() == 0) {
            /* tone_play(TONE_MAX_VOL); */
#if TCFG_MAX_VOL_PROMPT
            play_tone_file(get_tone_files()->max_vol);
#endif
        }
    }
    vol = __this->volume;
    app_send_message(APP_MSG_VOL_CHANGED, vol);
    printf("vol+:%d\n", __this->volume);
}


void mic_key_vol_down(void)
{
    s16 vol;
    if (__this->volume) {
        __this->volume --;
        mic_volume_set(__this->volume);
    }
    vol = __this->volume;
    app_send_message(APP_MSG_VOL_CHANGED, vol);
    printf("vol-:%d\n", __this->volume);
}

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SOURCE_EN | LE_AUDIO_AURACAST_SINK_EN)) || \
    (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_UNICAST_SOURCE_EN | LE_AUDIO_UNICAST_SINK_EN)) || \
    (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN)) || \
    (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_CIS_CENTRAL_EN | LE_AUDIO_JL_CIS_PERIPHERAL_EN))

static int get_mic_play_status(void)
{
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN))
    if (get_broadcast_app_mode_exit_flag()) {
        return LOCAL_AUDIO_PLAYER_STATUS_STOP;
    }
    if (get_broadcast_role() == 2) {
        //如果是作为接收端
        if (__this->last_run_local_audio_close) {
#if (LEA_BIG_FIX_ROLE == LEA_ROLE_AS_RX)
            if ((__this->onoff_as_broadcast_receive == 1) || __this->mic_local_need_open_flag) {
#else
            if (__this->onoff_as_broadcast_receive == 1) {
#endif
                return LOCAL_AUDIO_PLAYER_STATUS_PLAY;
            } else {
                return LOCAL_AUDIO_PLAYER_STATUS_STOP;
            }
            __this->last_run_local_audio_close = 0;
        }
    }

#if (LEA_BIG_FIX_ROLE == LEA_ROLE_AS_TX)
    if (get_broadcast_role()) {
        if (__this->mic_local_audio_resume_onoff) {
            return LOCAL_AUDIO_PLAYER_STATUS_PLAY;
        } else {
            return LOCAL_AUDIO_PLAYER_STATUS_STOP;
        }
    }
#endif

#if (LEA_BIG_FIX_ROLE == LEA_ROLE_AS_RX)
    if (__this->onoff || __this->mic_local_need_open_flag) {
#else
    if (__this->onoff) {
#endif
        return LOCAL_AUDIO_PLAYER_STATUS_PLAY;
    } else {
        return LOCAL_AUDIO_PLAYER_STATUS_STOP;
    }
#endif

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_CIS_CENTRAL_EN | LE_AUDIO_JL_CIS_PERIPHERAL_EN))
    if (get_connected_app_mode_exit_flag()) {
        return LOCAL_AUDIO_PLAYER_STATUS_STOP;
    }
#endif

    if (__this->onoff) {
        return LOCAL_AUDIO_PLAYER_STATUS_PLAY;
    } else {
        return LOCAL_AUDIO_PLAYER_STATUS_STOP;
    }
}

static int mic_local_audio_open(void)
{
    if (1) {//(get_mic_play_status() == LOCAL_AUDIO_PLAYER_STATUS_PLAY) {
        //打开本地播放
        int err = mic_start();
        if (!err) {
            log_error("mic_start fail!!!");
        }
    }
    return 0;
}

static int mic_local_audio_close(void)
{
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN))
    if (get_broadcast_role() == 2) {
        //作为接收端的情况
        if (mic_player_runing()) {
            mic_stop();
            __this->onoff_as_broadcast_receive = 1;
        } else {
            __this->onoff_as_broadcast_receive = 0;
        }
        __this->last_run_local_audio_close = 1;
        return 0;
    }
#endif

#if (TCFG_LE_AUDIO_APP_CONFIG & LE_AUDIO_JL_BIS_TX_EN) && (LEA_BIG_FIX_ROLE == LEA_ROLE_AS_TX)
    if (mic_player_runing()) {
        __this->mic_local_audio_resume_onoff = 1;
    } else {
        __this->mic_local_audio_resume_onoff = 0;
    }
#endif

    //作为非接收端的情况
    /* if (get_mic_play_status() == LOCAL_AUDIO_PLAYER_STATUS_PLAY) { */
    if (mic_player_runing()) {
        //关闭本地播放
        mic_stop();
    }
    return 0;
}


static void *mic_tx_le_audio_open(void *args)
{
    int err;
    void *le_audio = NULL;

    if (1) {//(get_mic_play_status() == LOCAL_AUDIO_PLAYER_STATUS_PLAY) {
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN))
        update_app_broadcast_deal_scene(LE_AUDIO_MUSIC_START);
#endif
        //打开广播音频播放
        struct le_audio_stream_params *params = (struct le_audio_stream_params *)args;
        le_audio = le_audio_stream_create(params->conn, &params->fmt);
        err = le_audio_mic_recorder_open((void *)&params->fmt, le_audio, params->latency);
        if (err != 0) {
            ASSERT(0, "recorder open fail");
        }
#if LEA_LOCAL_SYNC_PLAY_EN
        err = le_audio_player_open(le_audio, params);
        if (err != 0) {
            ASSERT(0, "player open fail");
        }
#endif

        __this->audio_state = APP_AUDIO_STATE_MUSIC;
        __this->volume = app_audio_get_volume(__this->audio_state);
        __this->onoff = 1;
    }

    return le_audio;
}

static int mic_tx_le_audio_close(void *le_audio)
{
    if (!le_audio) {
        return -EPERM;
    }

    //关闭广播音频播放
#if LEA_LOCAL_SYNC_PLAY_EN
    le_audio_player_close(le_audio);
#endif
    le_audio_mic_recorder_close();
    le_audio_stream_free(le_audio);

    app_audio_set_volume(APP_AUDIO_STATE_MUSIC, __this->volume, 1);
    __this->onoff = 0;

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN))
    update_app_broadcast_deal_scene(LE_AUDIO_MUSIC_STOP);
#endif
    return 0;
}

static int mic_rx_le_audio_open(void *rx_audio, void *args)
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

static int mic_rx_le_audio_close(void *rx_audio)
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

static int le_audio_mic_volume_pp(void)
{
    int ret = 0;

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN))
    if (__this->onoff == 0) {
        __this->onoff = 1;
        ret = app_broadcast_deal(LE_AUDIO_MUSIC_START);
#if (LEA_BIG_FIX_ROLE == LEA_ROLE_AS_TX)
        if (__this->mic_local_audio_resume_onoff == 0) {
            __this->mic_local_audio_resume_onoff = 1;
        }
#endif
    } else {
        __this->onoff = 0;
        ret = app_broadcast_deal(LE_AUDIO_MUSIC_STOP);
#if (LEA_BIG_FIX_ROLE == LEA_ROLE_AS_TX)
        if (__this->mic_local_audio_resume_onoff) {
            __this->mic_local_audio_resume_onoff = 0;
        }
#endif
    }
#endif

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_CIS_CENTRAL_EN | LE_AUDIO_JL_CIS_PERIPHERAL_EN))
    if (__this->onoff == 0) {
        __this->onoff = 1;
        ret = app_connected_deal(CONNECTED_MUSIC_START);
    } else {
        __this->onoff = 0;
        ret = app_connected_deal(CONNECTED_MUSIC_STOP);
    }
#endif

    return ret;
}

const struct le_audio_mode_ops le_audio_mic_ops = {
    .local_audio_open = mic_local_audio_open,
    .local_audio_close = mic_local_audio_close,
    .tx_le_audio_open = mic_tx_le_audio_open,
    .tx_le_audio_close = mic_tx_le_audio_close,
    .rx_le_audio_open = mic_rx_le_audio_open,
    .rx_le_audio_close = mic_rx_le_audio_close,
    .play_status = get_mic_play_status,
};

#endif
#endif





