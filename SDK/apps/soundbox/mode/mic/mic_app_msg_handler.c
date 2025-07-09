#include "key_driver.h"
#include "app_main.h"
#include "init.h"
#include "ui/ui_api.h"
#include "ui_manage.h"
#include "audio_config.h"
#include "media/includes.h"
#include "system/includes.h"
#include "mic.h"
#include "mic_player.h"
#include "app_le_broadcast.h"
#include "app_le_connected.h"
#include "le_broadcast.h"

#if TCFG_APP_MIC_EN

int mic_app_msg_handler(int *msg)
{
    const struct key_remap_table *table;

    if (false == app_in_mode(APP_MODE_MIC)) {
        return 0;
    }

    printf("live_mic_app_msg type:0x%x", msg[0]);
    u8 msg_type = msg[0];
#if (LEA_BIG_FIX_ROLE == LEA_ROLE_AS_RX) && !TCFG_KBOX_1T3_MODE_EN
#if (TCFG_LE_AUDIO_APP_CONFIG & LE_AUDIO_JL_BIS_RX_EN)
    if (get_broadcast_connect_status() &&
#elif (TCFG_LE_AUDIO_APP_CONFIG & LE_AUDIO_AURACAST_SINK_EN)
    if (get_auracast_status() == APP_AURACAST_STATUS_SYNC &&
#endif
        (msg_type == APP_MSG_MUSIC_PP  \
         || msg_type == APP_MSG_MUSIC_NEXT || msg_type == APP_MSG_MUSIC_PREV
        )) {

        printf("BIS receiving state does not support the event %d", msg_type);

        return 0;

    }
#endif


    switch (msg[0]) {
    case APP_MSG_CHANGE_MODE:
        printf("app msg key change mode\n");
        app_send_message(APP_MSG_GOTO_NEXT_MODE, 0);
        break;
    case APP_MSG_MIC_START:
        printf("app msg mic start\n");

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN))
        if (app_broadcast_deal(LE_AUDIO_APP_MODE_ENTER) > 0) {
            break;
        }
#endif

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_CIS_CENTRAL_EN | LE_AUDIO_JL_CIS_PERIPHERAL_EN))
        if (app_connected_deal(LE_AUDIO_APP_MODE_ENTER) > 0) {
            break;
        }
#endif

        mic_start();
        /* UI_REFLASH_WINDOW(true);//刷新主页并且支持打断显示 */
        break;
    case APP_MSG_MUSIC_PP:
        mic_volume_pp();
        break;
    case APP_MSG_VOL_UP:
        mic_key_vol_up();
        break;
    case APP_MSG_VOL_DOWN:
        mic_key_vol_down();
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


