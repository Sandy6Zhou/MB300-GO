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
#include "dsp_mode.h"
#include "iis_player.h"
#include "mic_player.h"


#if TCFG_APP_DSP_EN

#define LOG_TAG             "[APP_DSP]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
/* #define LOG_DUMP_ENABLE */
#define LOG_CLI_ENABLE
#include "debug.h"

struct dsp_opr {
    s16 volume ;
    u8 onoff;
    u8 audio_state; /*判断dsp模式使用模拟音量还是数字音量*/
};
static struct dsp_opr dsp_hdl = {0};
#define __this 	(&dsp_hdl)

static int le_audio_dsp_volume_pp(void);

// 打开dsp数据流操作
int dsp_start(void)
{
    if (__this->onoff == 1) {
        printf("dsp is aleady start\n");
        return true;
    }

    if (get_dsp_source()) { //根据输入源选择打开 IIS 或 MIC
        iis_player_open();
    } else {
        mic_player_open();
    }
    __this->audio_state = APP_AUDIO_STATE_MUSIC;
    __this->volume = app_audio_get_volume(__this->audio_state);
    __this->onoff = 1;
    return true;
}


// stop dsp 数据流
void dsp_stop(void)
{
    if (__this->onoff == 0) {
        printf("dsp is aleady stop\n");
        return;
    }
    if (get_dsp_source()) { //根据输入源选择关闭 IIS 或 MIC
        iis_player_close();
    } else {
        mic_player_close();
    }


    app_audio_set_volume(APP_AUDIO_STATE_MUSIC, __this->volume, 1);
    __this->onoff = 0;
}


/*-------------------------------------------------------------------*/
/**@brief   获取 dsp  播放状态
  @param    无
  @return   1:当前正在打开 0：当前正在关闭
  @note
 */
/*-------------------------------------------------------------------*/
u8 dsp_get_status(void)
{
    return __this->onoff;
}

/*-------------------------------------------------------------------*/
/*@brief   dsp 音量设置函数
  @param    需要设置的音量
  @return
  @note    在dsp 情况下针对了某些情景进行了处理，设置音量需要使用独立接口
*/
/*-------------------------------------------------------------------*/
int dsp_volume_set(s16 vol)
{
    app_audio_set_volume(__this->audio_state, vol, 1);
    printf("dsp vol: %d", __this->volume);
    __this->volume = vol;

    return true;
}

#endif


