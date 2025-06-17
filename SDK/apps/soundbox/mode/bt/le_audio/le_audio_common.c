/*********************************************************************************************
    *   Filename        : le_audio_common.c

    *   Description     :

    *   Author          : Weixin Liang

    *   Email           : liangweixin@zh-jieli.com

    *   Last modifiled  : 2023-8-18 19:09

    *   Copyright:(c)JIELI  2011-2022  @ , All Rights Reserved.
*********************************************************************************************/
#include "app_main.h"
#include "audio_base.h"
#include "wireless_trans.h"
#include "le_audio_stream.h"
#include "le_audio_player.h"
#include "le_broadcast.h"
#include "le_connected.h"
#include "app_le_broadcast.h"
#include "app_le_connected.h"
#include "app_le_auracast.h"
#include "classic/tws_event.h"
#include "classic/tws_api.h"
#include "bt_tws.h"
#include "le_audio_common.h"

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SOURCE_EN | LE_AUDIO_AURACAST_SINK_EN)) || \
    (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_UNICAST_SOURCE_EN | LE_AUDIO_UNICAST_SINK_EN)) || \
    (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN)) || \
    (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_CIS_CENTRAL_EN | LE_AUDIO_JL_CIS_PERIPHERAL_EN))

/**************************************************************************************************
  Static Prototypes
 **************************************************************************************************/
static int default_rx_le_audio_open(void *rx_audio, void *args);
static int default_rx_le_audio_close(void *rx_audio);
static int default_tx_le_audio_close(void *rx_audio);
static void *default_tx_le_audio_open(void *args);

static int le_audio_get_recorded_addr_num(void);
static int adv_report_macth_addr_add_timeout(void);
static void adv_report_macth_addr_del_timeout(void);
static int record_ble_report_mac_addr(u8 *mac_addr);

static inline void le_audio_common_mutex_pend(OS_MUTEX *mutex, u32 line);
static inline void le_audio_common_mutex_post(OS_MUTEX *mutex, u32 line);
/**************************************************************************************************
  Extern Global Variables
**************************************************************************************************/
extern const struct le_audio_mode_ops le_audio_a2dp_ops;
extern const struct le_audio_mode_ops le_audio_music_ops;
extern const struct le_audio_mode_ops le_audio_linein_ops;
extern const struct le_audio_mode_ops le_audio_iis_ops;
extern const struct le_audio_mode_ops le_audio_mic_ops;
extern const struct le_audio_mode_ops le_audio_spdif_ops;
extern const struct le_audio_mode_ops le_audio_fm_ops;
extern const struct le_audio_mode_ops le_audio_pc_ops;

/**************************************************************************************************
  Local Global Variables
**************************************************************************************************/
static u8 lea_product_test_name[28];
static u8 le_audio_pair_name[28];
static struct le_audio_mode_ops *broadcast_audio_switch_ops = NULL; /*!< le audio和local audio切换回调接口指针 */
static struct le_audio_mode_ops *connected_audio_switch_ops = NULL; /*!< le audio和local audio切换回调接口指针 */
const struct le_audio_mode_ops le_audio_default_ops = {
    .rx_le_audio_open = default_rx_le_audio_open,
    .rx_le_audio_close = default_rx_le_audio_close,
};



#define LE_AUDIO_MAX_RECORD_ADDR_NUM  3
#define LE_AUDIO_RECORDED_ADDR_WIRTE_VM     0
#define LE_AUDIO_FILTER_ADDR_TIMEOUT  0*1000L

static u8 le_audio_start_record_addr_flag = 0;
static u8 le_audio_curr_connect_mac_addr[6];
static u8 le_audio_last_connect_mac_addr[6];
static u8 le_audio_record_connect_mac_addr[LE_AUDIO_MAX_RECORD_ADDR_NUM][6];
static u8 le_audio_mac_addr_filter_mode = 0;
static u32 le_audio_addr_filter_timeout = 0;


static OS_MUTEX mutex;
static u8 le_audio_common_init_flag;  /*!< 广播初始化标志 */
/**************************************************************************************************
  Function Declarations
**************************************************************************************************/
void read_le_audio_product_name(void)
{
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN)) || \
    (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_CIS_CENTRAL_EN | LE_AUDIO_JL_CIS_PERIPHERAL_EN))
    int len = syscfg_read(CFG_LEA_PRODUCET_TEST_NAME, lea_product_test_name, sizeof(lea_product_test_name));
    if (len <= 0) {
        r_printf("ERR:Can not read the product test name\n");
        return;
    }

    put_buf((const u8 *)lea_product_test_name, sizeof(lea_product_test_name));
    r_printf("product_test_name:%s", lea_product_test_name);
#endif
}

void read_le_audio_pair_name(void)
{
#if ((TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN)) || \
    (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_CIS_CENTRAL_EN | LE_AUDIO_JL_CIS_PERIPHERAL_EN)) || \
    (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SINK_EN | LE_AUDIO_AURACAST_SOURCE_EN)))
    int len = syscfg_read(CFG_LEA_PAIR_NAME, le_audio_pair_name, sizeof(le_audio_pair_name));
    if (len <= 0) {
        r_printf("ERR:Can not read the le audio pair name\n");
        return;
    }

    put_buf((const u8 *)le_audio_pair_name, sizeof(le_audio_pair_name));
    y_printf("pair_name:%s", le_audio_pair_name);
#endif
}

const char *get_le_audio_product_name(void)
{
    return (const char *)lea_product_test_name;
}

const char *get_le_audio_pair_name(void)
{
    return (const char *)le_audio_pair_name;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 注册le audio和local audio切换回调接口
 *
 * @param ops:le audio和local audio切换回调接口结构体
 */
/* ----------------------------------------------------------------------------*/
static void le_audio_switch_ops_callback(void *ops)
{
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN)) || \
    (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SOURCE_EN | LE_AUDIO_JL_BIS_TX_EN)) || \
    (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SINK_EN | LE_AUDIO_JL_BIS_RX_EN))
    broadcast_audio_switch_ops = (struct le_audio_mode_ops *)ops;
#elif (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_CIS_CENTRAL_EN | LE_AUDIO_JL_CIS_PERIPHERAL_EN))
    connected_audio_switch_ops = (struct le_audio_mode_ops *)ops;
#endif
}

struct le_audio_mode_ops *get_broadcast_audio_sw_ops()
{
    return broadcast_audio_switch_ops;
}

struct le_audio_mode_ops *get_connected_audio_sw_ops()
{
    return connected_audio_switch_ops;
}

int le_audio_ops_register(u8 mode)
{
#if TCFG_KBOX_1T3_MODE_EN
    mode = APP_MODE_NULL;
#endif

    g_printf("le_audio_ops_register:%d", mode);

    switch (mode) {
#if TCFG_APP_BT_EN
    case APP_MODE_BT:
        le_audio_switch_ops_callback((void *)&le_audio_a2dp_ops);
        break;
#endif
#if TCFG_APP_MUSIC_EN
    case APP_MODE_MUSIC:
        le_audio_switch_ops_callback((void *)&le_audio_music_ops);
        break;
#endif
#if TCFG_APP_LINEIN_EN
    case APP_MODE_LINEIN:
        le_audio_switch_ops_callback((void *)&le_audio_linein_ops);
        break;
#endif
#if TCFG_APP_IIS_EN
    case APP_MODE_IIS:
        le_audio_switch_ops_callback((void *)&le_audio_iis_ops);
        break;
#endif
#if TCFG_APP_MIC_EN
    case APP_MODE_MIC:
        le_audio_switch_ops_callback((void *)&le_audio_mic_ops);
        break;
#endif
#if TCFG_APP_PC_EN
    case APP_MODE_PC:
        le_audio_switch_ops_callback((void *)&le_audio_pc_ops);
        break;
#endif
#if TCFG_APP_SPDIF_EN
    case APP_MODE_SPDIF:
        le_audio_switch_ops_callback((void *)&le_audio_spdif_ops);
        break;
#endif
#if TCFG_APP_FM_EN
    case APP_MODE_FM:
        le_audio_switch_ops_callback((void *)&le_audio_fm_ops);
        break;
#endif
    default:
        le_audio_switch_ops_callback((void *)&le_audio_default_ops);
        break;
    }
    return 0;
}

int le_audio_ops_unregister(void)
{
    le_audio_switch_ops_callback(NULL);
    return 0;
}

static int default_rx_le_audio_open(void *rx_audio, void *args)
{
    int err;
    struct le_audio_player_hdl *rx_audio_hdl = (struct le_audio_player_hdl *)rx_audio;

    if (!rx_audio) {
        return -EPERM;
    }

    //打开广播音频播放
    struct le_audio_stream_params *params = (struct le_audio_stream_params *)args;
    rx_audio_hdl->le_audio = le_audio_stream_create(params->conn, &params->fmt);
    rx_audio_hdl->rx_stream = le_audio_stream_rx_open(rx_audio_hdl->le_audio, params->fmt.coding_type);
    err = le_audio_player_open(rx_audio_hdl->le_audio, params);
    if (err != 0) {
        ASSERT(0, "player open fail");
    }

    return 0;
}

static int default_rx_le_audio_close(void *rx_audio)
{
    struct le_audio_player_hdl *rx_audio_hdl = (struct le_audio_player_hdl *)rx_audio;

    if (!rx_audio) {
        return -EPERM;
    }

    //关闭广播音频播放
    le_audio_player_close(rx_audio_hdl->le_audio);
    le_audio_stream_rx_close(rx_audio_hdl->rx_stream);
    le_audio_stream_free(rx_audio_hdl->le_audio);

    return 0;
}

void le_audio_working_status_switch(u8 en)
{
#if ((TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN)))
    if (en) {
        app_broadcast_open_in_other_mode();
    } else {
        if (app_get_current_mode()->name != APP_MODE_BT) {
            app_broadcast_close_in_other_mode();
        } else {
            app_broadcast_close(APP_BROADCAST_STATUS_STOP);
        }
    }
#endif


#if ((TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_CIS_CENTRAL_EN | LE_AUDIO_JL_CIS_PERIPHERAL_EN)))
    if (en) {
        app_connected_open_in_other_mode();
    } else {
        if (app_get_current_mode()->name != APP_MODE_BT) {
            app_connected_close_in_other_mode();
        } else {
            app_connected_close_all(APP_CONNECTED_STATUS_STOP);
        }
    }
#endif

}

#if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN)

#if (TCFG_KBOX_1T3_MODE_EN)
static void set_tws_sibling_mic_status(void *_data, u16 len, bool rx)
{
    u8 *data = (u8 *)_data;

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN))
    u8 local_mic_con_num = get_bis_connected_num();
#elif (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_CIS_CENTRAL_EN | LE_AUDIO_JL_CIS_PERIPHERAL_EN))
    u8 local_mic_con_num = get_connected_cis_num();
#endif

    if (rx) {
        r_printf("mic status tws rx %d\n", data[0]);

        if (tws_api_get_role() == TWS_ROLE_SLAVE) {
            ///从机没有连接mic，或者主机有连接mic的情况，从机都将le audio关掉
            if ((local_mic_con_num == 0) || (data[0])) {
                r_printf("slave working mic close\n");
                /* le_audio_working_status_switch(0); */
                app_send_message(APP_MSG_WIRELESS_MIC_CLOSE, 0);
            }
        } else {
            ///主机没有mic连接并且从机有mic连接，主机将le audio关掉
            if ((local_mic_con_num == 0) && (data[0])) {
                r_printf("master working mic close\n");
                /* le_audio_working_status_switch(0); */
                app_send_message(APP_MSG_WIRELESS_MIC_CLOSE, 0);
            }
        }

    }
}

REGISTER_TWS_FUNC_STUB(mic_sync_stub) = {
    .func_id = TWS_FUNC_ID_MIC_SYNC,
    .func    = set_tws_sibling_mic_status,
};

void le_audio_tws_sync_mic_status(void)
{

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN))
    u8 mic_con_num = get_bis_connected_num();
#elif (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_CIS_CENTRAL_EN | LE_AUDIO_JL_CIS_PERIPHERAL_EN))
    u8 mic_con_num = get_connected_cis_num();
#endif

    u8 data[1];
    data[0] = mic_con_num;
    tws_api_send_data_to_sibling(data, 1, TWS_FUNC_ID_MIC_SYNC);

    r_printf("tws_sync_mic_num: %d\n", mic_con_num);
}
#endif

int le_audio_sync_tws_event_handler(int *msg)
{
    struct tws_event *evt = (struct tws_event *)msg;
    u8 role = evt->args[0];
    u8 state = evt->args[1];
    u8 *bt_addr = evt->args + 3;
    u8 addr[6];

    printf(">>>>>>>>>>>>>>>>>>>>> le_auido_sync_tws_event_handler %d\n", evt->event);
    printf("tws role: %s\n", (tws_api_get_role() == TWS_ROLE_MASTER) ? "master" : "slave");
    switch (evt->event) {
    case TWS_EVENT_CONNECTED:
#if (TCFG_KBOX_1T3_MODE_EN)
#if 1
        le_audio_tws_sync_mic_status();
#else
        app_send_message(APP_MSG_WIRELESS_MIC_CLOSE, 0);
        app_send_message(APP_MSG_WIRELESS_MIC_OPEN, 1);
#endif
#endif

        break;
    case TWS_EVENT_CONNECTION_TIMEOUT:
        break;
    case TWS_EVENT_CONNECTION_DETACH:
        /*
         * TWS连接断开
         */
        if (app_var.goto_poweroff_flag) {
            break;
        }


#if (TCFG_KBOX_1T3_MODE_EN)
        /* if (role == TWS_ROLE_SLAVE) { */
        //r_printf("\n\nworking mic open\n\n");
        /* le_audio_working_status_switch(1); */
        app_send_message(APP_MSG_WIRELESS_MIC_CLOSE, 0);
        app_send_message(APP_MSG_WIRELESS_MIC_OPEN, 1);
        /* } */
#endif


        break;
    case TWS_EVENT_MONITOR_START:
        break;
    case TWS_EVENT_PHONE_LINK_DETACH:
        /*
         * 跟手机的链路LMP层已完全断开, 只有tws在连接状态才会收到此事件
         */
        break;
    case TWS_EVENT_ROLE_SWITCH:
        break;
    }
    return 0;
}

APP_MSG_HANDLER(le_audio_tws_msg_handler) = {
    .owner      = 0xff,
    .from       = MSG_FROM_TWS,
    .handler    = le_audio_sync_tws_event_handler,
};
#endif

void le_audio_all_restart(void)
{
#if (TCFG_LE_AUDIO_APP_CONFIG &  LE_AUDIO_JL_BIS_RX_EN)
    app_broadcast_close(APP_BROADCAST_STATUS_STOP);
    app_broadcast_open_with_role(1);
    return;
#endif

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_CIS_CENTRAL_EN | LE_AUDIO_JL_CIS_PERIPHERAL_EN))
    app_connected_close_all(APP_CONNECTED_STATUS_STOP);
    app_connected_open(0);
    return;
#endif

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SINK_EN | LE_AUDIO_AURACAST_SOURCE_EN))
    app_auracast_sink_close(APP_AURACAST_STATUS_STOP);
    app_auracast_sink_open();
    return;
#endif

}


static inline void le_audio_common_mutex_pend(OS_MUTEX *mutex, u32 line)
{

    int os_ret;
    os_ret = os_mutex_pend(mutex, 0);
    if (os_ret != OS_NO_ERR) {
        printf("%s err, os_ret:0x%x", __FUNCTION__, os_ret);
        ASSERT(os_ret != OS_ERR_PEND_ISR, "line:%d err, os_ret:0x%x", line, os_ret);
    }
}


static inline void le_audio_common_mutex_post(OS_MUTEX *mutex, u32 line)
{
    int os_ret;
    os_ret = os_mutex_post(mutex);
    if (os_ret != OS_NO_ERR) {
        printf("%s err, os_ret:0x%x", __FUNCTION__, os_ret);
        ASSERT(os_ret != OS_ERR_PEND_ISR, "line:%d err, os_ret:0x%x", line, os_ret);
    }
}

int le_audio_start_record_mac_addr(void)
{

    if (!g_bt_hdl.init_ok || app_var.goto_poweroff_flag) {
        return -EPERM;
    }

    if ((!get_le_audio_switch_onoff()) || get_le_audio_app_mode_exit_flag()) {
        return -EPERM;
    }


    if (get_le_audio_curr_role() != BROADCAST_ROLE_RECEIVER) {
        return -EPERM;
    }

    struct app_mode *mode = app_get_current_mode();
    if (mode && (mode->name == APP_MODE_BT) &&
        (bt_get_call_status() != BT_CALL_HANGUP)) {
        return -EPERM;
    }

    le_audio_start_record_addr_flag = 1;


#if LE_AUDIO_RECORDED_ADDR_WIRTE_VM
    for (int i = 0; i < LE_AUDIO_MAX_RECORD_ADDR_NUM; i++) {
        syscfg_read(VM_WIRELESS_RECORDED_ADDR0 + i, le_audio_record_connect_mac_addr[i], 6);
    }
#endif

    u8 temp[6] = {0};
    if (memcmp(le_audio_curr_connect_mac_addr, temp, 6)) {
        record_ble_report_mac_addr(le_audio_curr_connect_mac_addr);
    }

    return 0;
}

int le_audio_stop_record_mac_addr(void)
{
    if (!g_bt_hdl.init_ok || app_var.goto_poweroff_flag) {
        return -EPERM;
    }

    if ((!get_le_audio_switch_onoff()) || get_le_audio_app_mode_exit_flag()) {
        return -EPERM;
    }


    if (get_le_audio_curr_role() != BROADCAST_ROLE_RECEIVER) {
        return -EPERM;
    }

    struct app_mode *mode = app_get_current_mode();
    if (mode && (mode->name == APP_MODE_BT) &&
        (bt_get_call_status() != BT_CALL_HANGUP)) {
        return -EPERM;
    }

    le_audio_common_mutex_pend(&mutex, __LINE__);
    if (le_audio_start_record_addr_flag) {
        le_audio_start_record_addr_flag = 0;
    }

    if (le_audio_mac_addr_filter_mode) {
        le_audio_mac_addr_filter_mode = 0;
    }

    memset(le_audio_last_connect_mac_addr, 0, 6);

    le_audio_common_mutex_post(&mutex, __LINE__);

    return 0;

}

int le_audio_switch_source_device(u8 switch_mode) //0:切换设备后不过滤设备；1：切换设备后过滤处理只连接记录的设备
{
    if (!g_bt_hdl.init_ok || app_var.goto_poweroff_flag) {
        return -EPERM;
    }

    if ((!get_le_audio_switch_onoff()) || get_le_audio_app_mode_exit_flag()) {
        return -EPERM;
    }


    if (get_le_audio_curr_role() != BROADCAST_ROLE_RECEIVER) {
        return -EPERM;
    }

    struct app_mode *mode = app_get_current_mode();
    if (mode && (mode->name == APP_MODE_BT) &&
        (bt_get_call_status() != BT_CALL_HANGUP)) {
        return -EPERM;
    }

    u8 temp[6] = {0};
    u8 recorded_num = 0;

    if (!le_audio_start_record_addr_flag) {
        le_audio_start_record_mac_addr();
    }

    recorded_num = le_audio_get_recorded_addr_num();
    if (!recorded_num) {
        return -EPERM;
    }

    le_audio_common_mutex_pend(&mutex, __LINE__);
    if (recorded_num >= 2 && switch_mode) {
        le_audio_mac_addr_filter_mode = switch_mode;
    } else if (switch_mode) {
        printf("[error]The current number of records is insufficient");
        le_audio_mac_addr_filter_mode = 0;
    } else {
        le_audio_mac_addr_filter_mode = 0;
    }

    set_curr_ble_filter_adv_addr(le_audio_curr_connect_mac_addr);

    le_audio_common_mutex_post(&mutex, __LINE__);

    le_audio_all_restart();

    return 0;
}

int ble_resolve_adv_addr_mode(void)
{
    return le_audio_mac_addr_filter_mode;
}


void set_curr_ble_connect_addr(u8 *addr)
{
    if (!addr) {
        return;
    }
    memcpy(le_audio_curr_connect_mac_addr, addr, 6);

}


void set_curr_ble_filter_adv_addr(u8 *filter_addr)
{
    if (!filter_addr) {
        return;
    }
    memcpy(le_audio_last_connect_mac_addr, filter_addr, 6);

}


void get_curr_ble_filter_adv_addr(u8 *filter_addr)
{
    filter_addr = le_audio_last_connect_mac_addr;
}


int get_ble_report_addr_is_recorded(u8 *mac_addr)
{

    int ret = 0;
    le_audio_common_mutex_pend(&mutex, __LINE__);
    for (int i = 0; i < LE_AUDIO_MAX_RECORD_ADDR_NUM; i++) {
        if (!(memcmp(le_audio_record_connect_mac_addr[i], mac_addr, 6))) {
            ret = 1;
        }
    }

    le_audio_common_mutex_post(&mutex, __LINE__);
    return ret;

}


u8 get_adv_report_addr_filter_start_flag(void)
{
    return le_audio_start_record_addr_flag;

}

static int le_audio_get_recorded_addr_num(void)
{

    int num = 0;
    u8 temp[6] = {0};

    le_audio_common_mutex_pend(&mutex, __LINE__);

    for (int i = 0; i < LE_AUDIO_MAX_RECORD_ADDR_NUM; i++) {
        if ((memcmp(le_audio_record_connect_mac_addr[i], temp, 6))) {
            num++;
        }
    }
    le_audio_common_mutex_post(&mutex, __LINE__);

    return num;
}



static int record_ble_report_mac_addr(u8 *mac_addr)
{
    int ret = 0;
    u8 temp[6] = {0};
    static u8 discard_cnt = 0;
    int i = 0;
    if (!mac_addr) {
        return -EPERM;
    }

    if (!memcmp(mac_addr, temp, 6)) {
        return -EPERM;
    }


    for (i = 0; i < LE_AUDIO_MAX_RECORD_ADDR_NUM; i++) {
        if (!memcmp(mac_addr, le_audio_record_connect_mac_addr[i], 6)) {
            ret = 1;
            break;
        }
        if ((memcmp(le_audio_record_connect_mac_addr[i], temp, 6))) {
            memcpy(le_audio_record_connect_mac_addr[i], mac_addr, 6);
            ret = 2;
            break;
        }

    }

    if (i == LE_AUDIO_MAX_RECORD_ADDR_NUM) {
        printf("reached maxixmum number of records, discard [%d] addr:", discard_cnt);
        put_buf(le_audio_record_connect_mac_addr[discard_cnt], 6);
        memcpy(le_audio_record_connect_mac_addr[discard_cnt], mac_addr, 6);
        i =  discard_cnt;
        discard_cnt++;
        discard_cnt = (discard_cnt >= 3) ? 0 : discard_cnt;
    }
#if LE_AUDIO_RECORDED_ADDRWIRTE_VM
    syscfg_write(VM_WIRELESS_RECORDED_ADDR0 + i, le_audio_record_connect_mac_addr[i], 6);
#endif

    memcpy(le_audio_curr_connect_mac_addr, mac_addr, 6);

    return ret;
}

void le_audio_discard_recorded_addr(void)
{

    le_audio_common_mutex_pend(&mutex, __LINE__);

    for (int i = 0; i < LE_AUDIO_MAX_RECORD_ADDR_NUM; i++) {
        memset(le_audio_record_connect_mac_addr[i], 0, 6);
#if LE_AUDIO_RECORDED_ADDR_WIRTE_VM
        syscfg_write(VM_WIRELESS_RECORDED_ADDR0 + i, le_audio_record_connect_mac_addr[i], 6);
#endif
    }

    le_audio_mac_addr_filter_mode = 0;

    memset(le_audio_last_connect_mac_addr, 0, 6);

    le_audio_common_mutex_post(&mutex, __LINE__);

}

static void adv_report_macth_addr_timeout(void *priv)
{
#if LE_AUDIO_FILTER_ADDR_TIMEOUT
    memset(le_audio_last_connect_mac_addr, 0, 6);

    le_audio_mac_addr_filter_mode = 0;
#endif
}


static int adv_report_macth_addr_add_timeout(void)
{
#if LE_AUDIO_FILTER_ADDR_TIMEOUT
    if (!le_audio_addr_filter_timeout) {
        le_audio_addr_filter_timeout = sys_timeout_add(NULL, adv_report_macth_addr_timeout, LE_AUDIO_FILTER_ADDR_TIMEOUT);
    }
#endif
    return le_audio_addr_filter_timeout;
}


static void  adv_report_macth_addr_del_timeout(void)
{
#if LE_AUDIO_FILTER_ADDR_TIMEOUT
    if (le_audio_addr_filter_timeout) {
        sys_timeout_del(le_audio_addr_filter_timeout);
        le_audio_addr_filter_timeout = 0;
    }
#endif
}



bool ble_adv_report_match_by_addr(u8 *addr)
{

    bool find_remoter = 0;

    u8 *last_connect_mac_addr = NULL;
    u8 temp[6] = {0};


    if (!get_adv_report_addr_filter_start_flag()) {
        set_curr_ble_connect_addr(addr);
        return TRUE;
    }

    get_curr_ble_filter_adv_addr(last_connect_mac_addr);

    printf("last_connect_addr:\n");
    put_buf((u8 *)last_connect_mac_addr, 6);
    printf("curr_mac_addr\n");
    put_buf(addr, 6);

    if (!memcmp(last_connect_mac_addr, addr, 6)) {
        adv_report_macth_addr_add_timeout();
        return FALSE;
    }


    if (ble_resolve_adv_addr_mode()) {
        find_remoter  = get_ble_report_addr_is_recorded(addr);
    } else {
        find_remoter = 1;
    }



    if (find_remoter) {
        adv_report_macth_addr_del_timeout();
        record_ble_report_mac_addr(addr);
        return TRUE;
    } else {
        adv_report_macth_addr_add_timeout();
        return FALSE;
    }
}


void le_audio_common_init(void)
{

    printf("--func=%s", __FUNCTION__);
    int ret;

    if (le_audio_common_init_flag) {
        return;
    }

    int os_ret = os_mutex_create(&mutex);
    if (os_ret != OS_NO_ERR) {
        printf("%s %d err, os_ret:0x%x", __FUNCTION__, __LINE__, os_ret);
        ASSERT(0);
    }

    le_audio_common_init_flag = 1;
}
#endif

int le_audio_scene_deal(int scene)
{
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN))
    if (g_bt_hdl.work_mode == BT_MODE_BROADCAST) {
        return app_broadcast_deal(scene);
    }
#endif

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_CIS_CENTRAL_EN | LE_AUDIO_JL_CIS_PERIPHERAL_EN))
    if (g_bt_hdl.work_mode == BT_MODE_CIG) {
        return app_connected_deal(scene);
    }
#endif

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SINK_EN | LE_AUDIO_AURACAST_SOURCE_EN))
    if (g_bt_hdl.work_mode == BT_MODE_AURACAST) {
        return app_auracast_deal(scene);
    }
#endif

    return -EPERM;
}


int update_le_audio_deal_scene(int scene)
{
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN))
    return update_app_broadcast_deal_scene(scene);
#endif

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_CIS_CENTRAL_EN | LE_AUDIO_JL_CIS_PERIPHERAL_EN))
    return update_app_connected_deal_scene(scene);
#endif

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SINK_EN | LE_AUDIO_AURACAST_SOURCE_EN))
    return update_app_auracast_deal_scene(scene);
#endif

    return -EPERM;
}



u8 get_le_audio_app_mode_exit_flag()
{
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN))
    return get_broadcast_app_mode_exit_flag();
#endif

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_CIS_CENTRAL_EN | LE_AUDIO_JL_CIS_PERIPHERAL_EN))
    return get_connected_app_mode_exit_flag();
#endif

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SINK_EN | LE_AUDIO_AURACAST_SOURCE_EN))
    return get_auracast_app_mode_exit_flag();
#endif

    return 0;
}


u8 get_le_audio_curr_role(void) //1:transmitter; 2:recevier
{
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN))
    return get_broadcast_role();
#endif

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_CIS_CENTRAL_EN | LE_AUDIO_JL_CIS_PERIPHERAL_EN))
#if  (LEA_CIG_TRANS_MODE == LEA_TRANS_DUPLEX)
    return 1;
#else
    return get_connected_role();
#endif
#endif

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SINK_EN | LE_AUDIO_AURACAST_SOURCE_EN))
    return get_auracast_role();
#endif

    return 0;
}


u8 get_le_audio_switch_onoff(void) //1:on; 0:off
{
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN))
    return get_bis_switch_onoff();
#endif

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_CIS_CENTRAL_EN | LE_AUDIO_JL_CIS_PERIPHERAL_EN))
    return get_cis_switch_onoff();
#endif

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SINK_EN | LE_AUDIO_AURACAST_SOURCE_EN))
    return get_auracast_switch_onoff();
#endif

    return 0;
}


