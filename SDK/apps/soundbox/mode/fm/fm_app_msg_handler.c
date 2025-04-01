#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".fm_app_msg_handler.data.bss")
#pragma data_seg(".fm_app_msg_handler.data")
#pragma const_seg(".fm_app_msg_handler.text.const")
#pragma code_seg(".fm_app_msg_handler.text")
#endif
#include "key_driver.h"
#include "app_main.h"
#include "init.h"
#include "fm_api.h"
#include "fm_player.h"
#include "scene_switch.h"
#include "mic_effect.h"
#include "rcsp_fm_func.h"
#include "wireless_trans.h"
#include "le_broadcast.h"

#if (TCFG_SPI_LCD_ENABLE)
#include "ui/ui_api.h"
#endif

#if TCFG_APP_FM_EN
int fm_app_msg_handler(int *msg)
{
    if (false == app_in_mode(APP_MODE_FM)) {
        return 0;
    }
    u8 msg_type = msg[0];
#if  (TCFG_LE_AUDIO_APP_CONFIG & LE_AUDIO_JL_BIS_RX_EN) && (LEA_BIG_FIX_ROLE == LEA_ROLE_AS_RX) && !TCFG_KBOX_1T3_MODE_EN
    if (get_broadcast_role() == BROADCAST_ROLE_RECEIVER &&
        (msg_type == APP_MSG_FM_SCAN_ALL
         || msg_type == APP_MSG_FM_SCAN_ALL_DOWN || msg_type == APP_MSG_FM_SCAN_ALL_UP
#if LEA_BIG_VOL_SYNC_EN
         || msg_type == APP_MSG_VOL_UP || msg_type == APP_MSG_VOL_DOWN
#endif
         || msg_type == APP_MSG_FM_SCAN_DOWN || msg_type == APP_MSG_FM_SCAN_UP || msg_type == APP_MSG_MUSIC_PP
         || msg_type == APP_MSG_FM_START  || msg_type == APP_MSG_FM_PREV_STATION || msg_type == APP_MSG_FM_NEXT_STATION
         || msg_type == APP_MSG_FM_PREV_FREQ || msg_type == APP_MSG_FM_NEXT_FREQ
        )) {

        printf("BIS receiving state does not support the event %d", msg_type);

        return 0;

    }
#endif

#if (TCFG_SPI_LCD_ENABLE)
    if (key_is_ui_takeover()) {
        return false;
    }
#endif

    switch (msg[0]) {
    case APP_MSG_CHANGE_MODE:
        printf("app msg key change mode\n");
        app_send_message(APP_MSG_GOTO_NEXT_MODE, 0);
        break;
    case APP_MSG_FM_START:
        printf("app msg fm start\n");
        if (le_audio_scene_deal(LE_AUDIO_APP_MODE_ENTER) > 0) {
            break;
        }
        if (!fm_get_scan_flag()) {
            fm_player_open();
            /* fm_manage_start(); */
        }

#if (TCFG_PITCH_SPEED_NODE_ENABLE && FM_PLAYBACK_PITCH_KEEP)
        audio_pitch_default_parm_set(app_var.pitch_mode);
        fm_file_pitch_mode_init(app_var.pitch_mode);
#endif
        break;

    //暂停播放
    case APP_MSG_MUSIC_PP:
        printf("app msg fm pp\n");
        fm_volume_pp();
        break;
    //全自动搜台
    case APP_MSG_FM_SCAN_ALL:
    case APP_MSG_FM_SCAN_ALL_DOWN:
    case APP_MSG_FM_SCAN_ALL_UP:
        fm_scan_all();
        break;
    //半自动搜台
    case APP_MSG_FM_SCAN_DOWN:
        fm_scan_down();
        break;
    //半自动搜台
    case APP_MSG_FM_SCAN_UP:
        fm_scan_up();
        break;
    //上一台
    case APP_MSG_FM_PREV_STATION:
        fm_prev_station();
        break;
    //下一台
    case APP_MSG_FM_NEXT_STATION:
        fm_next_station();
        break;
    //上一个频率
    case APP_MSG_FM_PREV_FREQ:
        fm_prev_freq();
        break;
    //下一个频率
    case APP_MSG_FM_NEXT_FREQ:
        fm_next_freq();
        break;
    //增加音量
    case APP_MSG_VOL_UP:
        fm_volume_up();
        break;
    //降低音量
    case APP_MSG_VOL_DOWN:
        fm_volume_down();
        break;
    case APP_MSG_PITCH_UP:
#if TCFG_PITCH_SPEED_NODE_ENABLE
        printf("app msg fm pitch up\n");
        if (fm_player_runing()) {
            app_var.pitch_mode = fm_file_pitch_up(); //返回当前变调模式
        }
#endif
        break;
    case APP_MSG_PITCH_DOWN:
#if TCFG_PITCH_SPEED_NODE_ENABLE
        printf("app msg fm pitch down\n");
        if (fm_player_runing()) {
            app_var.pitch_mode = fm_file_pitch_down();
        }
#endif
        break;
#if 0
    case APP_MSG_VOCAL_REMOVE:
        printf("APP_MSG_VOCAL_REMOVE\n");
        music_vocal_remover_switch();
        break;
    case APP_MSG_MIC_EFFECT_ON_OFF:
        printf("APP_MSG_MIC_EFFECT_ON_OFF\n");
        if (mic_effect_player_runing()) {
            mic_effect_player_close();
        } else {
            mic_effect_player_open();
        }
        break;
#endif
    default:
        app_common_key_msg_handler(msg);
        break;
    }

#if (RCSP_MODE)
    rcsp_fm_msg_deal(msg[0]);
#endif

    return 0;
}

#endif

