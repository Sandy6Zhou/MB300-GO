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
#include "app_le_connected.h"
#include "tws_a2dp_play.h"

static u16 bt_mode_exit_timer = 0;
static u8 bt_next_mode = 0;

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
        bt_tws_poweroff();
    }
}
#endif

static int bt_exit_current_mode(u8 mode)
{
    int ret = 0;
    switch (mode) {
    case BT_MODE_SIGLE_BOX:

        break;
    case BT_MODE_TWS:
#if TCFG_USER_TWS_ENABLE
        int status = tws_api_get_tws_state();
        r_printf("tws_api_get_tws_state:%x %x\n", tws_api_get_tws_state(), (status & TWS_STA_SIBLING_CONNECTED && status & TWS_STA_PHONE_CONNECTED));
        if ((status & TWS_STA_SIBLING_CONNECTED && status & TWS_STA_PHONE_CONNECTED) \
            && app_get_a2dp_play_status()) {
            ret = -1;
        }
        bt_tws_onoff(0);
#endif
        break;
    case BT_MODE_BROADCAST:
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN))
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
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_CIS_CENTRAL_EN | LE_AUDIO_JL_CIS_PERIPHERAL_EN))
        le_audio_scene_deal(LE_AUDIO_APP_CLOSE);
        app_connected_uninit();
#endif
        break;
    case BT_MODE_3IN1:

        break;
    }
    return ret;
}

static int bt_enter_next_mode(u8 mode)
{
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
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_CIS_CENTRAL_EN | LE_AUDIO_JL_CIS_PERIPHERAL_EN))
        app_connected_init();
        le_audio_scene_deal(LE_AUDIO_APP_OPEN);
        app_connected_open(0);
#endif

        break;
    case BT_MODE_3IN1:

        break;
    }
    return 0;
}

static void bt_mode_exit_check(void *priv)
{
    u8 mode = *(u8 *)priv;
    switch (g_bt_hdl.work_mode) {
    case BT_MODE_TWS:
#if TCFG_USER_TWS_ENABLE
        if (!(tws_api_get_tws_state() & TWS_STA_SIBLING_CONNECTED) && app_get_a2dp_play_status() == 0) {
            sys_timer_del(bt_mode_exit_timer);
            bt_mode_exit_timer = 0;
            bt_enter_next_mode(mode);
        }
        break;
#endif
    default:
        break;
    }
}

int bt_work_mode_select(u8 mode)
{
#if TCFG_LE_AUDIO_APP_CONFIG == 0
    //le_audio不打开的情况下，不响应蓝牙工作模式切换
    return 0;
#endif
    //先释放当前模式资源
    r_printf("%s %d %d\n", __func__, mode, g_bt_hdl.work_mode);
    if (mode == g_bt_hdl.work_mode || bt_mode_exit_timer) {
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

    if (bt_exit_current_mode(g_bt_hdl.work_mode) != 0) {
        bt_next_mode = mode;
        bt_mode_exit_timer = sys_timer_add((void *)&bt_next_mode, bt_mode_exit_check, 50);
        return -1;
    }


    bt_enter_next_mode(mode);

    return 0;
}

void bt_work_mode_switch_to_next(void)
{
    static u8 work_mode = BT_MODE_SIGLE_BOX;

#if TCFG_LE_AUDIO_APP_CONFIG == 0
    //le_audio不打开的情况下，不响应蓝牙工作模式切换
    return;
#endif
    work_mode ++;

#if (TCFG_BT_BACKGROUND_ENABLE == 0)
    //非后台不在蓝牙模式不切换到TWS模式
    if ((app_in_mode(APP_MODE_BT) == 0) && (work_mode == BT_MODE_TWS)) {
        work_mode ++;
    }
#endif

#if TCFG_USER_TWS_ENABLE == 0
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN))
    if (work_mode == BT_MODE_TWS) {
        work_mode = BT_MODE_BROADCAST;
    }
#elif (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SOURCE_EN | LE_AUDIO_AURACAST_SINK_EN))
    if (work_mode == BT_MODE_TWS) {
        work_mode = BT_MODE_AURACAST;
    }
#elif (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_CIS_CENTRAL_EN | LE_AUDIO_JL_CIS_PERIPHERAL_EN))
    if (work_mode == BT_MODE_TWS) {
        work_mode = BT_MODE_CIG;
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

#if (!(TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_CIS_CENTRAL_EN | LE_AUDIO_JL_CIS_PERIPHERAL_EN)))
    if (work_mode ==  BT_MODE_CIG) {
        work_mode =  BT_MODE_3IN1;
    }
#endif
    if (work_mode == BT_MODE_3IN1) {
        work_mode = BT_MODE_SIGLE_BOX;
    }

    bt_work_mode_select(work_mode);
}

/*LE_AUDIO暂未支持低功耗*/
#if TCFG_LE_AUDIO_APP_CONFIG
static u8 mode_idle_query(void)
{
    if (g_bt_hdl.work_mode == BT_MODE_SIGLE_BOX || g_bt_hdl.work_mode == BT_MODE_TWS) {
        return 1;
    } else {
        return 0;
    }
}
REGISTER_LP_TARGET(mode_target) = {
    .name = "mode",
    .is_idle = mode_idle_query,
};
#endif
