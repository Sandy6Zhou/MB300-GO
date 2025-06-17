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
#include "loudspeaker.h"
#include "loudspeaker_mic_player.h"


#if TCFG_APP_LOUDSPEAKER_EN
#include "loudspeaker_iis_player.h"

#define LOG_TAG             "[APP_LOUDSPEAKER]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
/* #define LOG_DUMP_ENABLE */
#define LOG_CLI_ENABLE
#include "debug.h"

struct loudspeaker_opr {
    s16 volume ;
    u8 onoff;
    u8 audio_state; /*判断loudspeaker模式使用模拟音量还是数字音量*/
};
static struct loudspeaker_opr loudspeaker_hdl = {0};
#define __this 	(&loudspeaker_hdl)

static int le_audio_loudspeaker_volume_pp(void);

// 打开loudspeaker数据流操作
int loudspeaker_start(void)
{
    if (__this->onoff == 1) {
        printf("loudspeaker is aleady start\n");
        return true;
    }

    if (get_loudspeaker_source()) { //根据输入源选择打开 IIS 或 MIC
        loudspeaker_iis_player_open();
    } else {
        loudspeaker_mic_player_open();
    }
    __this->audio_state = APP_AUDIO_STATE_MUSIC;
    __this->volume = app_audio_get_volume(__this->audio_state);
    __this->onoff = 1;
    return true;
}


// stop loudspeaker 数据流
void loudspeaker_stop(void)
{
    if (__this->onoff == 0) {
        printf("loudspeaker is aleady stop\n");
        return;
    }
    if (get_loudspeaker_source()) { //根据输入源选择关闭 IIS 或 MIC
        loudspeaker_iis_player_close();
    } else {
        loudspeaker_mic_player_close();
    }


    app_audio_set_volume(APP_AUDIO_STATE_MUSIC, __this->volume, 1);
    __this->onoff = 0;
}

/*-------------------------------------------------------------------*/
/**@brief    loundspeaker 数据流 切换播放源
  @param    无
  @return   无
  @note
 */
/*-------------------------------------------------------------------*/
void loudspeaker_source_switch()
{
    loudspeaker_stop();
    set_loudspeaker_source(!get_loudspeaker_source());
    loudspeaker_start();
}

/*-------------------------------------------------------------------*/
/**@brief    loudspeaker 数据流 start stop 播放暂停切换
  @param    无
  @return   无
  @note
 */
/*-------------------------------------------------------------------*/
int loudspeaker_volume_pp(void)
{
    int ret = 0;

    if (ret <= 0) {
        if (__this->onoff) {
            loudspeaker_stop();
        } else {
            loudspeaker_start();
        }
    }

    printf("pp:%d \n", __this->onoff);
    /* UI_REFLASH_WINDOW(true); */
    return  __this->onoff;
}

/*-------------------------------------------------------------------*/
/**@brief   获取 loudspeaker  播放状态
  @param    无
  @return   1:当前正在打开 0：当前正在关闭
  @note
 */
/*-------------------------------------------------------------------*/
u8 loudspeaker_get_status(void)
{
    return __this->onoff;
}

/*-------------------------------------------------------------------*/
/*@brief   loudspeaker 音量设置函数
  @param    需要设置的音量
  @return
  @note    在loudspeaker 情况下针对了某些情景进行了处理，设置音量需要使用独立接口
*/
/*-------------------------------------------------------------------*/
int loudspeaker_volume_set(s16 vol)
{
    app_audio_set_volume(__this->audio_state, vol, 1);
    printf("loudspeaker vol: %d", __this->volume);
    __this->volume = vol;

    return true;
}

void loudspeaker_key_vol_up(void)
{
    s16 vol;
    audio_vol_index_t tmp;

    s16 max_vol;
    if (get_loudspeaker_source()) {
        max_vol = app_audio_volume_max_query(AppVol_LOUDSPEAKER_IIS);
    } else {
        max_vol = app_audio_volume_max_query(AppVol_LOUDSPEAKER_MIC);
    }
    if (__this->volume < max_vol) {
        __this->volume ++;
        loudspeaker_volume_set(__this->volume);
    } else {
        loudspeaker_volume_set(__this->volume);
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


void loudspeaker_key_vol_down(void)
{
    s16 vol;
    if (__this->volume) {
        __this->volume --;
        loudspeaker_volume_set(__this->volume);
    }
    vol = __this->volume;
    app_send_message(APP_MSG_VOL_CHANGED, vol);
    printf("vol-:%d\n", __this->volume);
}


#endif


