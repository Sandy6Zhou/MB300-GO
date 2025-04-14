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



#if LE_AUDIO_MIX_MIC_EN


struct le_audio_mic_recorder {
    void *stream;
};
static struct le_audio_mic_recorder *g_mix_mic_recorder = NULL;

static u8 is_need_resume_le_audio_mix_mic = 0;		//标记是否要恢复Mix mic的广播叠加


static void mix_mic_recorder_callback(void *private_data, int event)
{

}

static void le_audio_mix_mic_recorder_open(void)
{
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
    r_printf("uuid: 0x%x\n", uuid);
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
}


static void le_audio_mix_mic_recorder_close(void)
{
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
}


//在其它广播打开的前提下叠加 mic广播
void le_audio_mix_mic_open(void)
{
    if (get_le_audio_curr_role() == BROADCAST_ROLE_TRANSMITTER || get_le_audio_curr_role() == CONNECTED_ROLE_CENTRAL) {
        y_printf("LE Audio Mix Mic Open\n");
        le_audio_mix_mic_recorder_open();
    } else {
        r_printf(">>>>>>>>>>>>>>> Current LE Audio Role is not Tx!\n");
    }
}


//关闭叠加的 mic广播
void le_audio_mix_mic_close(void)
{
    r_printf("LE Audio Mix Mic Close\n");
    le_audio_mix_mic_recorder_close();
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

