#include "key_driver.h"
#include "app_main.h"
#include "init.h"
#include "ui/ui_api.h"
#include "ui_manage.h"
#include "audio_config.h"
#include "media/includes.h"
#include "system/includes.h"
#include "loudspeaker_iis_player.h"
#include "loudspeaker_mic_player.h"
#include "loudspeaker.h"

#if TCFG_APP_LOUDSPEAKER_EN

int loudspeaker_app_msg_handler(int *msg)
{
    const struct key_remap_table *table;

    if (false == app_in_mode(APP_MODE_LOUDSPEAKER)) {
        return 0;
    }
    switch (msg[0]) {
    case APP_MSG_CHANGE_MODE:
        printf("app msg key change mode\n");
        app_send_message(APP_MSG_GOTO_NEXT_MODE, 0);
        break;
    case APP_MSG_MUSIC_PP:
        loudspeaker_volume_pp();
        break;
    case APP_MSG_LOUDSPEAKER_OPEN:
        loudspeaker_start();
        break;
    case APP_MSG_LOUDSPEAKER_CLOSE:
        loudspeaker_stop();
        break;
    case APP_MSG_VOL_UP:
        loudspeaker_key_vol_up();
        break;
    case APP_MSG_VOL_DOWN:
        loudspeaker_key_vol_down();
        break;
    case APP_MSG_PITCH_UP:
        break;
    case APP_MSG_PITCH_DOWN:
        break;
    case APP_MSG_LOUDSPEAKER_SOURCE_SWITCH:
    case APP_MSG_WIRED_MIC_OFFLINE:
        loudspeaker_source_switch();
        break;
    case APP_MSG_WIRED_MIC_ONLINE:
        if (!loudspeaker_mic_player_runing()) {
            set_loudspeaker_source(0);
            loudspeaker_start();
        }
        break;
    default:
        app_common_key_msg_handler(msg);
        break;
    }

    return 0;
}



#endif



