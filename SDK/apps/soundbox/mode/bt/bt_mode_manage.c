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
#include "bt_ability.h"
#include "a2dp_player.h"

static u16 bt_mode_timer = 0;
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

#if (TCFG_BT_SUPPORT_LHDC_V5 ||TCFG_BT_SUPPORT_LHDC || TCFG_BT_SUPPORT_LDAC)
static void boardcast_set_hires_enable(u8 en)
{
#if (defined(TCFG_BT_SUPPORT_LHDC) && TCFG_BT_SUPPORT_LHDC)
    bt_set_support_lhdc_flag(en);
#endif

#if (defined(TCFG_BT_SUPPORT_LHDC_V5) && TCFG_BT_SUPPORT_LHDC_V5)
    bt_set_support_lhdc_v5_flag(en);
#endif

#if (defined(TCFG_BT_SUPPORT_LDAC) && TCFG_BT_SUPPORT_LDAC)
    bt_set_support_ldac_flag(en);
#endif
}

static u8 boardcast_detach_conn = 0;
static void *a2dp_play_devices = NULL;
static int boardcast_onoff_reconn_a2dp_channel(u8 onoff)
{
    u8 i = 0;
    void *devices[2] = {NULL};
    u8 addr[6];
    int num = btstack_get_conn_devices(devices, 2);
    boardcast_set_hires_enable(!onoff);
    boardcast_detach_conn = 0;
    for (i = 0; i < num; i ++) {
        bt_action_a2dp_detach(devices[i], btstack_get_device_mac_addr(devices[i]));
        boardcast_detach_conn ++;
        if (a2dp_player_get_btaddr(addr) && memcmp(addr, btstack_get_device_mac_addr(devices[i]), 6) == 0) {
            a2dp_play_devices = devices[i];
        }
    }
    return boardcast_detach_conn;
}

static int btstack_a2dp_boardcast_onoff_msg_handler(int *msg)
{
    struct bt_event *event = (struct bt_event *)msg;
    u8 match_event = 0;

    printf("bt_event_in_mode_change: %d\n", event->event);

#if TCFG_USER_TWS_ENABLE
    if (tws_api_get_role() == TWS_ROLE_SLAVE) {
        return 0;
    }
#endif

    switch (event->event) {
    case BT_STATUS_CONN_A2DP_CH:
        //链接上a2dp_ch的设备之前被抢占,创建定时器恢复a2dp播放
        if (boardcast_detach_conn) {
            g_printf("BT_STATUS_CONN_A2DP_CH when broadcast onff\n");
            if (btstack_get_conn_device(event->args) == a2dp_play_devices) {
                g_printf("send music_start\n");
                btstack_device_control(a2dp_play_devices, USER_CTRL_AVCTP_OPID_PLAY);
                a2dp_play_devices = NULL;
                /* sys_timeout_add(NULL, delay_resume_a2dp, 5000); */
            }
            boardcast_detach_conn --;
            y_printf("boardcast_detach_conn :%d\n", boardcast_detach_conn);

        }
        break;
    case BT_STATUS_DISCON_A2DP_CH:
        if (boardcast_detach_conn) {
            g_printf("BT_STATUS_DISCON_A2DP_CH when broadcast onoff\n");
            bt_action_a2dp_reconn(btstack_get_conn_device(event->args), event->args);
        }
        break;

    case BT_STATUS_FIRST_DISCONNECT:
    case BT_STATUS_SECOND_DISCONNECT:
        if (btstack_get_conn_device(event->args) == a2dp_play_devices) {
            a2dp_play_devices = NULL;
        }
        break;
    }

    return 0;
}

APP_MSG_HANDLER(scene_btstack_a2dp_boardcast_onoff_msg_handler) = {
    .owner      = 0xff,
    .from       = MSG_FROM_BT_STACK,
    .handler    = btstack_a2dp_boardcast_onoff_msg_handler,
};
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
        if ((status & TWS_STA_SIBLING_CONNECTED && status & TWS_STA_PHONE_CONNECTED)) {
            ret = -1;
        }
        bt_tws_onoff(0);
#endif
        break;
    case BT_MODE_BROADCAST:
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN))
        le_audio_scene_deal(LE_AUDIO_APP_CLOSE);
        app_broadcast_uninit();
#if (TCFG_BT_SUPPORT_LHDC_V5 ||TCFG_BT_SUPPORT_LHDC || TCFG_BT_SUPPORT_LDAC)
        if (boardcast_onoff_reconn_a2dp_channel(0)) {
            ret = -1;
        }
#endif
#endif
        break;
    case BT_MODE_AURACAST:
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SOURCE_EN | LE_AUDIO_AURACAST_SINK_EN))
        le_audio_scene_deal(LE_AUDIO_APP_CLOSE);
        app_auracast_uninit();
#if (TCFG_BT_SUPPORT_LHDC_V5 ||TCFG_BT_SUPPORT_LHDC || TCFG_BT_SUPPORT_LDAC)
        if (boardcast_onoff_reconn_a2dp_channel(0)) {
            ret = -1;
        }
#endif
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

static void bt_mode_check_enter(void *priv)
{
    u8 mode = *(u8 *)priv;
    switch (mode) {
    case BT_MODE_AURACAST:
    case BT_MODE_BROADCAST:
#if (TCFG_BT_SUPPORT_LHDC_V5 ||TCFG_BT_SUPPORT_LHDC || TCFG_BT_SUPPORT_LDAC)
        r_printf("bt_mode_check_enter\n");
        if (boardcast_detach_conn || a2dp_play_devices) {
            return;
        }
        sys_timer_del(bt_mode_timer);
        bt_mode_timer = 0;
        if (mode == BT_MODE_AURACAST) {
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SOURCE_EN | LE_AUDIO_AURACAST_SINK_EN))
            app_auracast_init();
            le_audio_scene_deal(LE_AUDIO_APP_OPEN);
            app_auracast_open();
#endif
        } else {
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN))
            app_broadcast_init();
            le_audio_scene_deal(LE_AUDIO_APP_OPEN);
            app_broadcast_open();
#endif
        }
#endif
        break;
    }
}

static int bt_enter_next_mode(u8 mode)
{
    int ret = 0;
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
    case BT_MODE_AURACAST:
        if (TCFG_BT_BACKGROUND_ENABLE || app_in_mode(APP_MODE_BT)) {
            clr_page_mode_active();
            dual_conn_page_device();
        }
#if (TCFG_BT_SUPPORT_LHDC_V5 ||TCFG_BT_SUPPORT_LHDC || TCFG_BT_SUPPORT_LDAC)
        ret = boardcast_onoff_reconn_a2dp_channel(1);
#endif
        if (ret == 0) {
            if (mode == BT_MODE_BROADCAST) {
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN))
                app_broadcast_init();
                le_audio_scene_deal(LE_AUDIO_APP_OPEN);
                app_broadcast_open();
                return 0;
#endif
            } else {
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SOURCE_EN | LE_AUDIO_AURACAST_SINK_EN))
                app_auracast_init();
                le_audio_scene_deal(LE_AUDIO_APP_OPEN);
                app_auracast_open();
                return 0;
#endif
            }
        }
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
    if (ret) {
        bt_mode_timer = sys_timer_add((void *)&bt_next_mode, bt_mode_check_enter, 50);
    }
    return ret;
}

static void bt_mode_exit_check(void *priv)
{
    u8 mode = *(u8 *)priv;
    u8 already_exit = 0;
    switch (g_bt_hdl.work_mode) {
    case BT_MODE_TWS:
#if TCFG_USER_TWS_ENABLE
        if (!(tws_api_get_tws_state() & TWS_STA_SIBLING_CONNECTED)) {
            already_exit = 1;
        }
        break;
#endif

    case BT_MODE_BROADCAST:
    case BT_MODE_AURACAST:
#if (TCFG_BT_SUPPORT_LHDC_V5 ||TCFG_BT_SUPPORT_LHDC || TCFG_BT_SUPPORT_LDAC)
        if (boardcast_detach_conn == 0) {
            already_exit = 1;
        }
#endif
        break;

    default:
        break;
    }
    if (already_exit) {
        sys_timer_del(bt_mode_timer);
        bt_mode_timer = 0;
        bt_enter_next_mode(mode);
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
    if (mode == g_bt_hdl.work_mode || bt_mode_timer) {
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

    bt_next_mode = mode;

    if (bt_exit_current_mode(g_bt_hdl.work_mode) != 0) {
        bt_mode_timer = sys_timer_add((void *)&bt_next_mode, bt_mode_exit_check, 50);
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


