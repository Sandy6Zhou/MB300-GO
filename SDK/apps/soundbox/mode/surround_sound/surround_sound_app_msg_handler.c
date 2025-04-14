
#include "key_driver.h"
#include "app_main.h"
#include "init.h"
#include "ui/ui_api.h"
#include "ui_manage.h"
#include "scene_switch.h"
#include "mic_effect.h"
#include "app_le_broadcast.h"
#include "app_le_connected.h"
#include "le_broadcast.h"
#include "audio_config.h"
#include "app_le_auracast.h"
#include "surround_sound.h"

#if TCFG_APP_SURROUND_SOUND_EN

int surround_sound_app_msg_handler(int *msg)
{
    if (false == app_in_mode(APP_MODE_SURROUND_SOUND)) {
        return 0;
    }

    printf("surround_sound_app_msg type:0x%x", msg[0]);
    u8 msg_type = msg[0];

    switch (msg[0]) {
    case APP_MSG_CHANGE_MODE:
        puts("app msg key change mode\n");
        app_send_message(APP_MSG_GOTO_NEXT_MODE, 0);
        break;
    case APP_MSG_SURROUND_SOUND_START:
        puts("app msg surround sound start\n");
        if (le_audio_scene_deal(LE_AUDIO_APP_MODE_ENTER) > 0) {
            break;
        }
        /* UI_REFLASH_WINDOW(true);//刷新主页并且支持打断显示 */
        break;
    case APP_MSG_MUSIC_PP:
        break;

    case APP_MSG_VOL_UP:
        //需要关闭音量同步才生效
#if (LEA_BIG_VOL_SYNC_EN==0)
        app_audio_volume_up(1);
#endif
        break;
    case APP_MSG_VOL_DOWN:
        //需要关闭音量同步才生效
#if (LEA_BIG_VOL_SYNC_EN==0)
        app_audio_volume_down(1);
#endif
        break;
    case APP_MSG_PITCH_UP:
        break;
    case APP_MSG_PITCH_DOWN:
        break;
    default:
        app_common_key_msg_handler(msg);
        break;
    }
    return 0;
}



#endif


