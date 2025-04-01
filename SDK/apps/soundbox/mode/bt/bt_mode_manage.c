#include "system/includes.h"
#include "app_config.h"
#include "app_main.h"
#include "bt_tws.h"
#include "dual_conn.h"
#include "local_tws_player.h"
#include "le_broadcast.h"
#include "app_le_broadcast.h"
#include "app_le_auracast.h"
#include "fm_api.h"
#include "dual_conn.h"

#if TCFG_USER_TWS_ENABLE
void bt_tws_onoff(u8 onoff)
{
    if (onoff == 1) {
        bt_tws_poweron();
    } else {
#if TCFG_LOCAL_TWS_ENABLE
        local_tws_player_close();
#endif
        tws_delete_pair_timer();
        tws_api_cancel_all();
        /* tws_api_cancle_create_connection(); */
        /* tws_api_cancle_wait_pair(); */
        bt_tws_poweroff();
    }
}
#endif

int bt_work_mode_select(u8 mode)
{
    //先释放当前模式资源
    r_printf("%s %d %d\n", __func__, mode, g_bt_hdl.work_mode);
    if (mode == g_bt_hdl.work_mode) {
        printf("same work mode  : %d", g_bt_hdl.work_mode);
        return 0;
    }

    if ((mode == BT_MODE_BROADCAST || mode == BT_MODE_AURACAST) && (bt_get_call_status() != BT_CALL_HANGUP)) {
        printf("le_audio cannot be turned on during the Bluetooth call");
        return 0;
    }

#if TCFG_APP_FM_EN
    if ((mode == BT_MODE_BROADCAST || mode == BT_MODE_AURACAST) && fm_get_scan_flag()) {
        printf("le_audio cannot be turned on during the FM channel search process ");
        return 0;
    }
#endif

    if (mode == 0) {
        mode = BT_MODE_SIGLE_BOX;
    }

    switch (g_bt_hdl.work_mode) {
    case BT_MODE_SIGLE_BOX:

        break;
    case BT_MODE_TWS:
#if TCFG_USER_TWS_ENABLE
        bt_tws_onoff(0);
#endif
        break;
    case BT_MODE_BROADCAST:
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN))
        //app_broadcast_close(APP_BROADCAST_STATUS_STOP);
        le_audio_scene_deal(LE_AUDIO_APP_CLOSE);
        app_broadcast_uninit();
#endif
        break;
    case BT_MODE_AURACAST:
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SOURCE_EN | LE_AUDIO_AURACAST_SINK_EN))
        le_audio_scene_deal(LE_AUDIO_APP_CLOSE);
        app_auracast_uninit();
#endif
        break;
    case BT_MODE_CIG:

        break;
    case BT_MODE_3IN1:

        break;
    }

    g_bt_hdl.last_work_mode = g_bt_hdl.work_mode;
    g_bt_hdl.work_mode = mode;

    switch (mode) {
    case BT_MODE_SIGLE_BOX:
        if (TCFG_BT_BACKGROUND_ENABLE || app_in_mode(APP_MODE_BT)) {
            clr_page_mode_active();
            dual_conn_page_device();
        }
        break;
    case BT_MODE_TWS:
#if TCFG_USER_TWS_ENABLE
        clr_page_mode_active();
        bt_tws_onoff(1);
#endif
        break;
    case BT_MODE_BROADCAST:
        if (TCFG_BT_BACKGROUND_ENABLE || app_in_mode(APP_MODE_BT)) {
            clr_page_mode_active();
            dual_conn_page_device();
        }
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN))
        app_broadcast_init();
        le_audio_scene_deal(LE_AUDIO_APP_OPEN);
        app_broadcast_open();
#endif
        break;
    case BT_MODE_AURACAST:
        if (TCFG_BT_BACKGROUND_ENABLE || app_in_mode(APP_MODE_BT)) {
            clr_page_mode_active();
            dual_conn_page_device();
        }
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SOURCE_EN | LE_AUDIO_AURACAST_SINK_EN))
        app_auracast_init();
        le_audio_scene_deal(LE_AUDIO_APP_OPEN);
        app_auracast_open();
#endif
        break;
    case BT_MODE_CIG:

        break;
    case BT_MODE_3IN1:

        break;
    }
    return 0;
}

void bt_work_mode_switch_to_next(void)
{
    static u8 work_mode = BT_MODE_SIGLE_BOX;
    work_mode ++;
#if TCFG_USER_TWS_ENABLE == 0
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN))
    if (work_mode == BT_MODE_TWS) {
        work_mode = BT_MODE_BROADCAST;
    }
#elif (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SOURCE_EN | LE_AUDIO_AURACAST_SINK_EN))
    if (work_mode == BT_MODE_TWS) {
        work_mode = BT_MODE_AURACAST;
    }
#endif
#endif

#if (!(TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN)))
    if (work_mode ==  BT_MODE_BROADCAST) {
        work_mode =  BT_MODE_AURACAST;
    }
#endif

#if (!(TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SOURCE_EN | LE_AUDIO_AURACAST_SINK_EN)))
    if (work_mode ==  BT_MODE_AURACAST) {
        work_mode =  BT_MODE_CIG;
    }
#endif
    if (work_mode == BT_MODE_CIG) {
        work_mode = BT_MODE_SIGLE_BOX;
    }

    bt_work_mode_select(work_mode);
}

