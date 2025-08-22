
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".spdif_player.data.bss")
#pragma data_seg(".spdif_player.data")
#pragma const_seg(".spdif_player.text.const")
#pragma code_seg(".spdif_player.text")
#endif
#include "jlstream.h"
#include "sdk_config.h"
#include "system/timer.h"
#include "system/includes.h"
#include "spdif_player.h"
#include "spdif_file.h"
#include "app_config.h"
#include "app_main.h"
#include "effects/audio_pitchspeed.h"
#include "effect/effects_default_param.h"
#include "audio_config.h"
#include "scene_switch.h"
#include "audio_config_def.h"
#include "effects/audio_vbass.h"
#include "le_audio_player.h"
#include "app_le_auracast.h"
#include "spdif.h"
#include "le_audio_recorder.h"
#include "le_broadcast.h"
#if AUDIO_EQ_LINK_VOLUME
#include "effects/eq_config.h"
#endif
#include "audio_effect_demo.h"

#if TCFG_SPDIF_ENABLE

//开关spdif后，是否保持变调状态
#define SPDIF_PLAYBACK_PITCH_KEEP          0

struct spdif_player {
    struct jlstream *stream;
    u8 open_flag;
    s8 spdif_pitch_mode;
};
static struct spdif_player *g_spdif_player = NULL;

//默认不是 mute 状态
static u8 spdif_last_vol_state = 1;

static void spdif_player_callback(void *private_data, int event)
{
    struct spdif_player *player = g_spdif_player;
    struct jlstream *stream = (struct jlstream *)private_data;

    switch (event) {
    case STREAM_EVENT_START:
#if TCFG_DAC_NODE_ENABLE
        spdif_last_vol_state = !app_audio_get_dac_digital_mute();	//可能一进spdif模式就已经是mute的状态, 需更新mute状态
        app_audio_mute(spdif_last_vol_state);
#endif
        app_send_message(APP_MSG_MUTE_CHANGED, !spdif_last_vol_state);
#if AUDIO_VBASS_LINK_VOLUME
        vbass_link_volume();
#endif
#if AUDIO_EQ_LINK_VOLUME
        eq_link_volume();
#endif
#if AUDIO_AUTODUCK_LINK_VOLUME
        autoduck_link_volume();
#endif
        break;
    }
}

//spdif 切源的时候解mute
void spdif_switch_source_unmute(void)
{
    spdif_last_vol_state = 1;
#if TCFG_DAC_NODE_ENABLE
    app_audio_mute(spdif_last_vol_state);
#endif
    app_send_message(APP_MSG_MUTE_CHANGED, !spdif_last_vol_state);
}

/*
 * @description: 打开spdif 数据流
 * @return：0 - 成功。其它值失败
 * @node:
 */
int spdif_player_open(void)
{
    int err;
    struct spdif_player *player;

    printf("\n>>>>>>>>>>>> spdif_player_open!\n\n");

#if TCFG_LOCAL_TWS_ENABLE
    struct local_tws_stream_params *local_tws_fmt = {0};

    struct stream_enc_fmt fmt = {
        .channel = LOCAL_TWS_CODEC_CHANNEL,
        .bit_width = LOCAL_TWS_CODEC_BIT_WIDTH,
        .frame_dms = LOCAL_TWS_CODEC_FRAME_LEN,
        .sample_rate = LOCAL_TWS_CODEC_SAMPLERATE,
        .bit_rate = LOCAL_TWS_CODEC_BIT_RATE,
        .coding_type = LOCAL_TWS_CODEC_TYPE,
    };
#endif
    u16 uuid = jlstream_event_notify(STREAM_EVENT_GET_PIPELINE_UUID, (int)"spdif");
    if (uuid == 0) {
        return -EFAULT;
    }


    player = malloc(sizeof(*player));
    if (!player) {
        return -ENOMEM;
    }

    player->spdif_pitch_mode = PITCH_0;

    player->stream = jlstream_pipeline_parse(uuid, NODE_UUID_SPDIF);

    if (!player->stream) {
        err = -ENOMEM;
        goto __exit0;
    }


    jlstream_node_ioctl(player->stream, NODE_UUID_SOURCE, NODE_IOC_SET_PRIV_FMT, SPDIF_DATA_DMA_LEN);

    //设置检测丢包间隔为2帧时间
    float thread = 2.0f;
    jlstream_node_ioctl(player->stream, NODE_UUID_PLAY_SYNC, NODE_IOC_SET_FMT, (int)&thread);

    jlstream_set_callback(player->stream, player->stream, spdif_player_callback);
    jlstream_set_scene(player->stream, STREAM_SCENE_SPDIF);
#if defined(TCFG_VIRTUAL_SURROUND_EFF_MODULE_NODE_ENABLE) && TCFG_VIRTUAL_SURROUND_EFF_MODULE_NODE_ENABLE
    jlstream_add_thread(player->stream, "media0");
    jlstream_add_thread(player->stream, "media1");
#if defined(CONFIG_CPU_BR28)
    jlstream_add_thread(player->stream, "media2");
#endif
#endif
#if TCFG_LOCAL_TWS_ENABLE
    err = jlstream_ioctl(player->stream, NODE_IOC_SET_ENC_FMT, (int)&fmt);
    if (err == 0) {
        err = jlstream_start(player->stream);
    }
#else
    err = jlstream_start(player->stream);
#endif
    if (err) {
        goto __exit1;
    }

    player->open_flag = 1;
    g_spdif_player = player;
    printf("\n>>>>>>>>>>>> spdif player open succ\n\n");

    app_send_message(APP_MSG_SPDIF_STATUS_UPDATE, 0);
    return 0;

__exit1:
    jlstream_release(player->stream);
__exit0:
    free(player);
    return err;
}

bool spdif_player_runing()
{
    return g_spdif_player != NULL;
}

/*
 * @description: 关闭spdif 数据流
 * @return：
 * @node:
 */
void spdif_player_close()
{
    struct spdif_player *player = g_spdif_player;

    if (!player) {
        return;
    }

    player->open_flag = 0;

    jlstream_stop(player->stream, 50);
    jlstream_release(player->stream);

    free(player);
    g_spdif_player = NULL;

    jlstream_event_notify(STREAM_EVENT_CLOSE_PLAYER, (int)"spdif");
    app_send_message(APP_MSG_SPDIF_STATUS_UPDATE, 0);
}

void update_spdif_player_mute_state(void)
{
    spdif_last_vol_state = !app_audio_get_dac_digital_mute();	//更新保存数据流前的音量mute状态
}

/* 重启 spdif 数据流 */
static void spdif_restart(void)
{
    if (app_get_current_mode()->name != APP_MODE_SPDIF) {
        return;
    }
    printf("================ restart spdif player\n");

    spdif_last_vol_state = !app_audio_get_dac_digital_mute();	//保存数据流前的音量状态

    if (g_spdif_player) {
        if (g_spdif_player->open_flag) {
            //打开播放器的情况下才需要关player
            spdif_player_close();
        }
    } else {
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN)) || (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SOURCE_EN | LE_AUDIO_AURACAST_SINK_EN))
        if (get_le_audio_curr_role()) {
            //关闭广播音频播放
            void *le_audio = spdif_get_le_audio_hdl();
            if (le_audio) {
#if LEA_LOCAL_SYNC_PLAY_EN
                le_audio_player_close(le_audio);
#endif
                le_audio_spdif_recorder_close();
            }
        }
#endif
    }

    spdif_stop();
    spdif_release();
    spdif_init();
    spdif_start();
    spdif_stream_start();

    if (spdif_is_hdmi_source()) {
        //当前是HDMI源输入
        if (!hdmi_is_online()) {
            //拔出导致的重启
            spdif_hdmi_set_push_data_en(0);	//不往后面推数，交给CEC通信控制
        }
    }
}


/*
 * @description: 通过消息队列重启 spdif 数据流
 * @return：0 表示消息发送成功
 * @node:
 */
int spdif_restart_by_taskq(void)
{
    int msg[2];
    msg[0] = (int)spdif_restart;
    msg[1] = 0;
    int ret = os_taskq_post_type("app_core", Q_CALLBACK, 2, msg);
    spdif_stream_stop();
    return ret;
}

static void delay_open_spdif_player(void *priv)
{
    printf(">>>>>>>>>>>> Enter Func: %s\n", __func__);
    spdif_open_player_by_taskq(0);
}

/* 打开 spdif player */
void spdif_open_player(void)
{
    if (app_get_current_mode()->name != APP_MODE_SPDIF) {
        return;
    }
    printf("================ open spdif player\n");
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN)) || (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SOURCE_EN | LE_AUDIO_AURACAST_SINK_EN))
    if (get_le_audio_curr_role() == 1) { //打开广播的数据流
        y_printf(">> Spdif_LE_AUDIO Control in spdif_tx_le_open!");
    }
#else
    if (0) {}
#endif
    else {
        if (!g_spdif_player) {
            spdif_player_open();
#if (TCFG_PITCH_SPEED_NODE_ENABLE && SPDIF_PLAYBACK_PITCH_KEEP)
            audio_pitch_default_parm_set(app_var.pitch_mode);
            spdif_file_pitch_mode_init(app_var.pitch_mode);
#endif
            music_vocal_remover_update_parm();
        }
    }
}

/*
 * @description: 通过消息队列打开 spdif 数据流
 * @return：0 表示消息发送成功
 * @node:
 */
int spdif_open_player_by_taskq(int delay_us)
{
    int ret = 0;
    if (delay_us == 0) {
        int msg[1];
        msg[0] = (int)spdif_open_player;
        /* msg[1] = 0; */
        ret = os_taskq_post_type("app_core", Q_CALLBACK, 1, msg);
    } else {
        sys_timeout_add(NULL, delay_open_spdif_player, delay_us);
    }
    return ret;
}

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN)) || (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SOURCE_EN | LE_AUDIO_AURACAST_SINK_EN))
void spdif_open_le_audio(void)
{
    if (app_get_current_mode()->name == APP_MODE_SPDIF) {
        y_printf(">>>>>>>>>>>>>>>>>>>>>>> Call Func: le_audio_scene_deal(LE_AUDIO_MUSIC_START)\n");
        le_audio_scene_deal(LE_AUDIO_MUSIC_START);
    }
}

int spdif_open_le_audio_by_taskq(void)
{
    int msg[1];
    msg[0] = (int)spdif_open_le_audio;
    /* msg[1] = 0; */
    int ret = os_taskq_post_type("app_core", Q_CALLBACK, 1, msg);
    return ret;
}


static void spdif_le_audio_music_stop(int arg)
{
    if (app_get_current_mode()->name == APP_MODE_SPDIF) {
        y_printf(">>>>>>>>>>>>>>>>>>>>>>> Call Func: le_audio_scene_deal(LE_AUDIO_MUSIC_STOP)\n");
        le_audio_scene_deal(LE_AUDIO_MUSIC_STOP);
    }
}

int spdif_le_audio_music_stop_by_taskq(void)
{
    int msg[2];
    msg[0] = (int)spdif_le_audio_music_stop;
    msg[1] = 0;
    int ret = os_taskq_post_type("app_core", Q_CALLBACK, 2, msg);
    return ret;
}

#endif


//变调接口
int spdif_file_pitch_up()
{
    struct spdif_player *player = g_spdif_player;
    if (!player) {
        return -1;
    }
    float pitch_param_table[] = {-12, -10, -8, -6, -4, -2, 0, 2, 4, 6, 8, 10, 12};
    player->spdif_pitch_mode++;
    if (player->spdif_pitch_mode > ARRAY_SIZE(pitch_param_table) - 1) {
        player->spdif_pitch_mode = ARRAY_SIZE(pitch_param_table) - 1;
    }
    printf("play pitch up+++%d\n", player->spdif_pitch_mode);
    int ret = spdif_file_set_pitch(player->spdif_pitch_mode);
    ret = (ret == true) ? player->spdif_pitch_mode : -1;
    return ret;
}

int spdif_file_pitch_down()
{
    struct spdif_player *player = g_spdif_player;
    if (!player) {
        return -1;
    }
    player->spdif_pitch_mode--;
    if (player->spdif_pitch_mode < 0) {
        player->spdif_pitch_mode = 0;
    }
    printf("play pitch down---%d\n", player->spdif_pitch_mode);
    int ret = spdif_file_set_pitch(player->spdif_pitch_mode);
    ret = (ret == true) ? player->spdif_pitch_mode : -1;
    return ret;
}

int spdif_file_set_pitch(enum _pitch_level pitch_mode)
{
    struct spdif_player *player = g_spdif_player;
    float pitch_param_table[] = {-12, -10, -8, -6, -4, -2, 0, 2, 4, 6, 8, 10, 12};
    if (pitch_mode > ARRAY_SIZE(pitch_param_table) - 1) {
        pitch_mode = ARRAY_SIZE(pitch_param_table) - 1;
    }
    pitch_speed_param_tool_set pitch_param = {
        .pitch = pitch_param_table[pitch_mode],
        .speed = 1,
    };
    if (player) {
        player->spdif_pitch_mode = pitch_mode;
        return jlstream_node_ioctl(player->stream, NODE_UUID_PITCH_SPEED, NODE_IOC_SET_PARAM, (int)&pitch_param);
    }
    return -1;
}

void spdif_file_pitch_mode_init(enum _pitch_level pitch_mode)
{
    struct spdif_player *player = g_spdif_player;
    if (player) {
        player->spdif_pitch_mode = pitch_mode;
    }
}

#else


bool spdif_player_runing()
{
    return 0;
}


#endif

