/*************************************************************************************************/
/*!
*  \file      le_audio_mix_mic_recorder.c
*
*  \brief	  在其它非mic模式下的广播下叠加mic一起广播出去
*
*  Copyright (c) 2011-2025 ZhuHai Jieli Technology Co.,Ltd.
*
*/
/*************************************************************************************************/

#include "jlstream.h"
#include "le_audio_recorder.h"
#include "sdk_config.h"
#include "le_audio_stream.h"
#include "audio_config_def.h"
#include "audio_config.h"
#include "media/sync/audio_syncts.h"
#include "asm/dac.h"
#include "audio_cvp.h"
#include "wireless_trans.h"
#include "le_audio_mix_mic_recorder.h"
#include "le_broadcast.h"
#include "le_connected.h"
#include "mic_effect.h"

extern void mic_effect_ram_code_unload();
extern void mic_effect_ram_code_load();


#if LE_AUDIO_MIX_MIC_EN


struct le_audio_mic_recorder {
    void *stream;
};
static struct le_audio_mic_recorder *g_mix_mic_recorder = NULL;

static u8 is_need_resume_le_audio_mix_mic = 0;		//标记是否要恢复Mix mic的广播叠加

static u8 is_need_resume_local_mic_eff = 0;		//标记当关闭Mix Mic关闭后是否要恢复本地的混响

static void mix_mic_recorder_callback(void *private_data, int event)
{

}

static void le_audio_mix_mic_recorder_open(void)
{
#if (LE_AUDIO_MIX_MIC_EFFECT_EN == 0)
    //不带混响的叠加Mic广播
    int err = 0;
    u32 latency = LE_AUDIO_MIX_MIC_LATENCY;
    u16 irq_points = 256;
    if (g_mix_mic_recorder) {
        return;
    }
    if (!g_mix_mic_recorder) {
        g_mix_mic_recorder = zalloc(sizeof(struct le_audio_mic_recorder));
        if (!g_mix_mic_recorder) {
            return;
        }
    }

    u16 uuid = jlstream_event_notify(STREAM_EVENT_GET_PIPELINE_UUID, (int)"mic_le_audio");
    r_printf("[Le Audio Mix Mic] uuid: 0x%x\n", uuid);
    g_mix_mic_recorder->stream = jlstream_pipeline_parse(uuid, NODE_UUID_ADC);

    if (!g_mix_mic_recorder->stream) {
        return;
    }

    jlstream_set_callback(g_mix_mic_recorder->stream, NULL, mix_mic_recorder_callback);

    jlstream_set_scene(g_mix_mic_recorder->stream, STREAM_SCENE_MIC);
    jlstream_node_ioctl(g_mix_mic_recorder->stream, NODE_UUID_SOURCE, NODE_IOC_SET_PRIV_FMT, irq_points);

    if (latency == 0) {
        latency = 85000;
    }

    y_printf("le_audio mix mic latency: %d, irq_points:%d\n", latency, irq_points);
    jlstream_node_ioctl(g_mix_mic_recorder->stream, NODE_UUID_CAPTURE_SYNC, NODE_IOC_SET_PARAM, latency);
    jlstream_start(g_mix_mic_recorder->stream);
    printf("le_audio Mix Mic jlstream stream start!\n");

#else
    //叠加混响Mic的广播
    mic_effect_ram_code_load();
    int err = 0;
    u32 latency = LE_AUDIO_MIX_MIC_LATENCY;
    u16 irq_points = 256;
    if (g_mix_mic_recorder) {
        return;
    }
    if (!g_mix_mic_recorder) {
        g_mix_mic_recorder = zalloc(sizeof(struct le_audio_mic_recorder));
        if (!g_mix_mic_recorder) {
            return;
        }
    }
    //如果本地混响打开的情况下，先关闭混响，然后标记结束后要恢复本地混响
    if (mic_effect_player_runing()) {
        mic_effect_player_close();
        is_need_resume_local_mic_eff = 1;
    }

    u16 uuid = jlstream_event_notify(STREAM_EVENT_GET_PIPELINE_UUID, (int)"mic_effect");
    r_printf("[Le Audio Mix Mic] uuid: 0x%x\n", uuid);
    g_mix_mic_recorder->stream = jlstream_pipeline_parse_by_node_name(uuid, "MicEff");
    if (!g_mix_mic_recorder->stream) {
        goto exit1;
    }
    jlstream_set_scene(g_mix_mic_recorder->stream, STREAM_SCENE_MIC);
    jlstream_node_ioctl(g_mix_mic_recorder->stream, NODE_UUID_SOURCE, NODE_IOC_SET_PRIV_FMT, irq_points);
    jlstream_node_ioctl(g_mix_mic_recorder->stream, NODE_UUID_VOCAL_TRACK_SYNTHESIS, NODE_IOC_SET_PRIV_FMT, irq_points);

    jlstream_add_thread(g_mix_mic_recorder->stream, "mic_effect1");
    jlstream_add_thread(g_mix_mic_recorder->stream, "mic_effect2");
    /* jlstream_add_thread(g_mix_mic_recorder->stream, "mic_effect3"); */

    if (latency == 0) {
        latency = 85000;
    }
    y_printf("le_audio mix mic latency: %d, irq_points:%d\n", latency, irq_points);
    jlstream_node_ioctl(g_mix_mic_recorder->stream, NODE_UUID_CAPTURE_SYNC, NODE_IOC_SET_PARAM, latency);
    jlstream_start(g_mix_mic_recorder->stream);

    printf("le_audio Mix Mic(Add Mic Eff) jlstream stream start!\n");
    return;

exit1:
    if (is_need_resume_local_mic_eff) {
        is_need_resume_local_mic_eff = 0;
        mic_effect_player_open();
    }
    return;

#endif
}


static void le_audio_mix_mic_recorder_close(void)
{
#if (LE_AUDIO_MIX_MIC_EFFECT_EN == 0)
    struct le_audio_mic_recorder *mic_recorder = g_mix_mic_recorder;
    if (!mic_recorder) {
        return;
    }
    if (mic_recorder->stream) {
        jlstream_stop(mic_recorder->stream, 0);
        jlstream_release(mic_recorder->stream);
    }
    free(mic_recorder);
    g_mix_mic_recorder = NULL;
    jlstream_event_notify(STREAM_EVENT_CLOSE_PLAYER, (int)"mic_le_audio");
#else

    //先关闭混合广播Mic，再根据是否需要恢复本地混响
    struct le_audio_mic_recorder *mic_recorder = g_mix_mic_recorder;
    if (!mic_recorder) {
        return;
    }
    //这里说明mix mic是正在跑的，关闭mix mic广播需要打开本地mic eff
    is_need_resume_local_mic_eff = 1;
    if (mic_recorder->stream) {
        jlstream_stop(mic_recorder->stream, 0);
        jlstream_release(mic_recorder->stream);
    }
    mic_effect_ram_code_unload();
    free(mic_recorder);
    g_mix_mic_recorder = NULL;
    jlstream_event_notify(STREAM_EVENT_CLOSE_PLAYER, (int)"mic_le_audio");
    if (is_need_resume_local_mic_eff) {
        is_need_resume_local_mic_eff = 0;
        mic_effect_player_open();
    }
#endif
}


//在其它广播打开的前提下叠加 mic广播
void le_audio_mix_mic_open(void)
{
    u32 rets;//, reti;
    __asm__ volatile("%0 = rets":"=r"(rets));
    if (get_le_audio_curr_role() == BROADCAST_ROLE_TRANSMITTER || get_le_audio_curr_role() == CONNECTED_ROLE_CENTRAL) {
        y_printf("LE Audio Mix Mic Open\n");
        /* printf(">>>>> Func:%s, Line:%d, addr:%x\n", __func__, __LINE__, rets); */
        le_audio_mix_mic_recorder_open();
    } else {
        r_printf(">>>>>>>>>>>>>>> Current LE Audio Role is not Tx!\n");
    }
}


//关闭叠加的 mic广播
void le_audio_mix_mic_close(void)
{
    u32 rets;//, reti;
    __asm__ volatile("%0 = rets":"=r"(rets));
    r_printf("LE Audio Mix Mic Close\n");
    /* printf(">>>>> Func:%s, Line:%d, addr:%x\n", __func__, __LINE__, rets); */
    le_audio_mix_mic_recorder_close();
}

int get_micEff2LeAudio_switch_status()
{
#if LE_AUDIO_MIX_MIC_EFFECT_EN
    if (g_mix_mic_recorder) {
        return 1;
    }
    return 0;
#else
    return 0;
#endif
}

u8 is_le_audio_mix_mic_recorder_running(void)
{
    if (g_mix_mic_recorder) {
        return 1;
    }
    return 0;
}

void set_need_resume_le_audio_mix_mic(u8 en)
{
    is_need_resume_le_audio_mix_mic = en;
}

u8 get_is_need_resume_le_audio_mix_mic(void)
{
    return is_need_resume_le_audio_mix_mic;
}



#endif

