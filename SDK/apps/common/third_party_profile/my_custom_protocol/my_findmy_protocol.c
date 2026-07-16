/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : my_findmy_protocol.c
 * Brief      : 设备自定义findmy协议模块
 * Version    : V1.0.0
 * Author     : 周森达(zhousenda@jimiiot.com)
 * Date       : 2025-12-22
************************************************************************************************/

#include "sdk_config.h"
#include "app_msg.h"
// #include "earphone.h"
#include "bt_tws.h"
#include "app_main.h"
#include "btstack/avctp_user.h"
#include "multi_protocol_main.h"
#include "classic/hci_lmp.h"
#include "bt_event_func.h"
#include "dual_conn.h"
#include "my_common.h"
#include "my_findmy_protocol.h"

#if (THIRD_PARTY_PROTOCOLS_SEL & CUSTOM_DEMO_EN && (MY_FINDMY_EN == 1))

#define DEV_CUST_UUID       0xFEE5  // 自定义UUID
#define FLAG_TYPE_VALUE     0x06    // google的flag值
#define CON_ADV_OBJ_MAX_NUM 2       // 一路google，一路ios
#define ADV_INTERVAL        800     // 800*0.625ms=500ms
#define BLE_NOTIFY_SEND_BUF_MAX_SIZE    1024

typedef enum {
    GOOGLE_ADV_TYPE,
    APPLE_ADV_TYPE
} MY_ADV_TYPE;

ADV_HDL_S con_adv_obj_hdl[2] = {
    {.handle = NULL, .valid = 1},
    {.handle = NULL, .valid = 1}
};
ADV_HDL_S no_con_adv_obj_hdl[2] = {
    {.handle = NULL, .valid = 1},
    {.handle = NULL, .valid = 1}
};

void *custom_demo_spp_hdl = NULL;
static u8 battery_capacity = 100;   // 预留电量

static uint8_t connect_id = 0xff;
static uint16_t ble_server_rx_index = 0;
static uint8_t uart_ble_server_buf[BLE_NOTIFY_SEND_BUF_MAX_SIZE];
static u8 s_defer_ems_after_cid = 0; // 5505发完且发送空闲后再schedule EMS

static bool ble_server_send_done = true;

/*
 * ============================================================================
 * 【DC 关机抑制】DC 通讯离线时禁止蓝牙可被连接/重连
 * - my_dc_uart 检测离线后投递 MY_MSG_BLE_DC_POWER_OFF → ble_dc_power_off_handle
 * - DC 恢复在线后投递 MY_MSG_BLE_DC_POWER_ON  → ble_dc_power_on_restore
 * - dual_conn 通过 ble_dc_is_power_suppressed() 阻止抑制期内重开 scan/conn
 * ============================================================================
 */
static u8 g_ble_dc_power_suppressed = 0; /* 1=DC关机抑制中，connect/disconnect 回调与 dual_conn 均受控 */
static u8 s_ble_link = 0;                /* BLE GATT 是否连接 */
static u8 s_bt_classic_link_cnt = 0;   /* 经典 BT 连接数，用于图标 OR 逻辑 */

static void dc_ble_icon_sync(void)
{
    // DC关机抑制时，不更新蓝牙图标
    if (g_ble_dc_power_suppressed)
    {
        return;
    }

    // 任一蓝牙链路存在时，点亮DC蓝牙图标
    if (s_ble_link || s_bt_classic_link_cnt)
    {
        my_log_printf(1, "[DC_BT_ICON] on ble=%u classic=%u", s_ble_link, s_bt_classic_link_cnt);
        my_send_msg(MOD_MAIN, MOD_DC_UART, MY_MSG_DC_POLL_START);
    }
    else
    {
        my_log_printf(1, "[DC_BT_ICON] off ble=%u classic=%u", s_ble_link, s_bt_classic_link_cnt);
        my_send_msg(MOD_MAIN, MOD_DC_UART, MY_MSG_DC_POLL_STOP);
    }
}

/************************************************************************
**@brief: 查询 DC 关机抑制标志（供 dual_conn 等模块守卫用）
**@return: 1=抑制中（不可连接/不可被回调反向开广播）  0=正常
*************************************************************************/
u8 ble_dc_is_power_suppressed(void)
{
    return g_ble_dc_power_suppressed;
}

void my_findmy_ble_disconnect(void);

/*
 * 经典 BT 关机侧处理：断链 + 关扫描/连接（仅发 HCI 命令，非阻塞）
 * 注意：不调用 btstack_exit_edr，保持协议栈运行以便 DC 恢复后 ble_dc_classic_restore
 */
static void ble_dc_classic_shutdown(void)
{
#if TCFG_APP_BT_EN
    extern u8 hci_standard_connect_check(void);

    /* 1. 关闭 dual_conn 自动 page/scan 调度，避免断链后又自动重连 */
#if TCFG_USER_TWS_ENABLE
    tws_dual_conn_close();
#else
    dual_conn_close();
#endif

    /* 2. 已建链：发 POWER_OFF 断开经典 BT（A2DP/AVRCP 等） */
    if (bt_get_curr_channel_state() != 0)
    {
        bt_cmd_prepare(USER_CTRL_POWER_OFF, 0, NULL);
    }
    /* 3. 未建链但在 page/inquiry：取消进行中的连接请求 */
    else if (hci_standard_connect_check())
    {
        bt_cmd_prepare(USER_CTRL_PAGE_CANCEL, 0, NULL);
        bt_cmd_prepare(USER_CTRL_CONNECTION_CANCEL, 0, NULL);
    }

    /* 4. 关闭可被手机发现与连接 */
    bt_cmd_prepare(USER_CTRL_WRITE_SCAN_DISABLE, 0, NULL);
    bt_cmd_prepare(USER_CTRL_WRITE_CONN_DISABLE, 0, NULL);
#else
    lmp_hci_write_scan_enable(0);
#endif
}

/* DC 恢复在线：重开 inquiry + page scan，恢复可被连接 */
static void ble_dc_classic_restore(void)
{
#if TCFG_APP_BT_EN
    bt_discovery_and_connectable_using_loca_mac_addr(1, 1);
#else
    lmp_hci_write_scan_enable((1 << 1) | 1);
#endif
}

/* 若 SPP 已连接则主动断开（与 BLE GATT 断链并行，避免 APP 仍走串口透传） */
static void ble_dc_spp_disconnect(void)
{
    if ((custom_demo_spp_hdl != NULL) &&
        (app_spp_get_hdl_remote_addr(custom_demo_spp_hdl) != NULL))
    {
        app_spp_disconnect(custom_demo_spp_hdl);
    }
}

static bool ble_data_send_enable[2] = {0};

static void start_adv(ADV_HDL_S *adv_obj_hdl, u8 enable);
static int ble_server_att_send_buffered(u16 len);
static void ble_server_flush_pending_tx(void);
static void ble_server_try_schedule_deferred_ems(void);
/*************************************************
                  BLE 相关内容
*************************************************/
// 自定义服务配置数据
const uint8_t my_profile_data[] = {
    //////////////////////////////////////////////////////
    //
    // 0x0001 PRIMARY_SERVICE  1800
    //
    //////////////////////////////////////////////////////
    0x0a, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x28, 0x00, 0x18,

    /* CHARACTERISTIC,  2a00, READ | WRITE | DYNAMIC, */
    // 0x0002 CHARACTERISTIC 2a00 READ | WRITE | DYNAMIC
    0x0d, 0x00, 0x02, 0x00, 0x02, 0x00, 0x03, 0x28, 0x0a, 0x03, 0x00, 0x00, 0x2a,
    // 0x0003 VALUE 2a00 READ | WRITE | DYNAMIC
    0x08, 0x00, 0x0a, 0x01, 0x03, 0x00, 0x00, 0x2a,

    //////////////////////////////////////////////////////
    //
    // 0x0004 PRIMARY_SERVICE  FEE9
    //
    //////////////////////////////////////////////////////
    0x0a, 0x00, 0x02, 0x00, 0x04, 0x00, 0x00, 0x28, 0xE5, 0xFE,

    /* CHARACTERISTIC,  FEB5, WRITE | WRITE_WITHOUT_RESPONSE | DYNAMIC, */
    // 0x0005 CHARACTERISTIC FEB5 WRITE | WRITE_WITHOUT_RESPONSE | DYNAMIC
    0x0d, 0x00, 0x02, 0x00, 0x05, 0x00, 0x03, 0x28, 0x0C, 0x06, 0x00, 0xB5, 0xFE,
    // 属性: 0x0C = WRITE(0x08) + WRITE_NO_RESPONSE(0x04)
    // 0x0006 VALUE FEB5 WRITE | WRITE_WITHOUT_RESPONSE | DYNAMIC
    0x08, 0x00, 0x0C, 0x01, 0x06, 0x00, 0xB5, 0xFE,

    /* CHARACTERISTIC,  FEB6, NOTIFY, */
    // 0x0007 CHARACTERISTIC FEB6 NOTIFY
    0x0d, 0x00, 0x02, 0x00, 0x07, 0x00, 0x03, 0x28, 0x10, 0x08, 0x00, 0xB6, 0xFE,
    // 属性: 0x10 = NOTIFY
    // 0x0008 VALUE FEB6 NOTIFY
    0x08, 0x00, 0x10, 0x00, 0x08, 0x00, 0xB6, 0xFE,
    // 0x0009 CLIENT_CHARACTERISTIC_CONFIGURATION
    0x0a, 0x00, 0x0a, 0x01, 0x09, 0x00, 0x02, 0x29, 0x00, 0x00,

    // END
    0x00, 0x00,
};

//
// characteristics <--> handles
//
#define ATT_CHARACTERISTIC_2a00_01_VALUE_HANDLE 0x0003
#define ATT_CHARACTERISTIC_ae01_01_VALUE_HANDLE 0x0006
#define ATT_CHARACTERISTIC_ae02_01_VALUE_HANDLE 0x0008
#define ATT_CHARACTERISTIC_ae02_01_CLIENT_CONFIGURATION_HANDLE 0x0009

// 检查BLE是否被连上
bool check_connect_id_enable(void)
{
    if (connect_id == 0xff)
    {
        return false;
    }
    return true;
}

/************************************************************************
**@brief: 设置哪一路广播生效
**@param[in] index: 目前0代表google，1代表ios
**@param[in] status: 0:不生效,1:生效
*************************************************************************/
void set_adv_valid_status(int index, int status)
{
    con_adv_obj_hdl[index].valid = status;
    no_con_adv_obj_hdl[index].valid = status;
}

/************************************************************************
**@brief: 获取广播响应数据
**@param[in] adv_type:  获取哪种广播类型(google或ios)
**@param[in] adv_data:  存储的广播数据buffer
**@param[in] adv_len:   存储的广播数据buffer长度
**@param[in] scan_data: 存储的扫描响应数据buffer
**@param[in] scan_len:  存储的扫描响应数据buffer长度
*************************************************************************/
static void get_adv_data(MY_ADV_TYPE adv_type, uint8_t **adv_data, uint8_t *adv_len, uint8_t **scan_data, uint8_t *scan_len)
{
    u8 *pos;
    static u8 advertising_data[31];
    static u8 adv_data_len = 0;
    static u8 scan_response_data[31];
    static u8 scan_rsp_data_len = 0;

    const char *local_name = NULL;
    u8 name_len = 0;
    u8 lic_len = 0;
    u16 uuid_user = 0;
    u8 manufacturer_value[6] = {0};
    const u8 *edr_addr = NULL;
    u8 flags_value;                     // flag type value 0x06

    //scan_response_data---------------------------------------------------------------
    local_name = bt_get_local_name();

    //device name
    pos = &scan_response_data[0];
    name_len = strlen((const char *)local_name);

    *pos++ = name_len + 1;  // devname len + type
    *pos++ = HCI_EIR_DATATYPE_SHORTENED_LOCAL_NAME;
    memcpy(pos, local_name, name_len);
    pos += name_len;

    // uuid
    uuid_user = DEV_CUST_UUID;

    *pos++ = sizeof(uuid_user) + 1; // uuid len + type
    *pos++ = HCI_EIR_DATATYPE_MORE_16BIT_SERVICE_UUIDS;
    memcpy(pos, (u8 *)&uuid_user, sizeof(uuid_user));
    pos += sizeof(uuid_user);

    // mac+batt_percent
    edr_addr = bt_get_mac_addr();

    manufacturer_value[0] = edr_addr[5];
    manufacturer_value[1] = edr_addr[4];
    manufacturer_value[2] = edr_addr[3];
    manufacturer_value[3] = edr_addr[2];
    manufacturer_value[4] = edr_addr[1];
    manufacturer_value[5] = edr_addr[0];

    *pos++ = sizeof(manufacturer_value) + 2 + 1;
    *pos++ = HCI_EIR_DATATYPE_MORE_32BIT_SERVICE_UUIDS;

    memcpy(pos,manufacturer_value,sizeof(manufacturer_value));
    pos += sizeof(manufacturer_value);

    // batt_percent
    *pos++ = 0x02;                  // 数据类型
    *pos++ = (u8)battery_capacity;  // 电量百分比

    scan_rsp_data_len = ((u32)pos - (u32)(&scan_response_data[0]));

    //advertising_data-------------------------------------------------------------------------------
    pos = &advertising_data[0];

    if (GOOGLE_ADV_TYPE == adv_type)
    {
        flags_value = FLAG_TYPE_VALUE;
        *pos++ = sizeof(flags_value) + 1;
        *pos++ = HCI_EIR_DATATYPE_FLAGS;
        *pos++ = flags_value;

        lic_len = sizeof(gConfigParam.lic_gg.hex);
        *pos++ = lic_len + 1;
        *pos++ = HCI_EIR_DATATYPE_SERVICE_DATA;  // google类型0x16

        memcpy(pos, gConfigParam.lic_gg.hex, lic_len);
        pos += lic_len;

        // google数据不需要电量值
        adv_data_len = ((u32)pos - (u32)(&advertising_data[0]));
    }
    else 
    {
        lic_len = sizeof(gConfigParam.lic_ff.hex);
        *pos++ = lic_len + 1;
        *pos++ = HCI_EIR_DATATYPE_MANUFACTURER_SPECIFIC_DATA;

        memcpy(pos, gConfigParam.lic_ff.hex, lic_len);
        pos += lic_len;

        //TODO 电量后续看情况是否增加
        if (battery_capacity >= 80)
            advertising_data[6] = 0x20;     // FULL - 满电量
        else if (battery_capacity >= 20)
            advertising_data[6] = 0x60;     // Medium - 中电量
        else if (battery_capacity >= 5)
            advertising_data[6] = 0xA0;     // Low - 低电量
        else
            advertising_data[6] = 0xE0;     // Critically low - 严重亏电

        adv_data_len = ((uint32_t)pos - (uint32_t)(&advertising_data[0]));
    }

    // printf("advertising_data:");
    // put_buf((u8 *)advertising_data, adv_data_len);
    // printf("scan_response_data:");
    // put_buf((u8 *)scan_response_data, scan_rsp_data_len);

    *adv_data = advertising_data;
    *adv_len = adv_data_len;
    *scan_data = scan_response_data;
    *scan_len = scan_rsp_data_len;
}

int my_findmy_adv_enable(u8 enable)
{
    int i = 0;
    u8 has_conn = 0;
    printf("my_findmy_adv_enable:%d", enable);

    if (enable)
    {
        /*
         * 恢复广播时实时检查连接句柄，避免 connect_id 残留导致恢复到不可连接广播。
         * 这里仅检查两个“可连接对象”的连接句柄：
         * - 任一对象已连接 -> 保持不可连接广播（用于连接后状态广播）
         * - 均未连接      -> 打开可连接广播（供APP发起连接）
         */
        has_conn =
            ((con_adv_obj_hdl[GOOGLE_ADV_TYPE].handle &&
              app_ble_get_hdl_con_handle(con_adv_obj_hdl[GOOGLE_ADV_TYPE].handle)) ||
             (con_adv_obj_hdl[APPLE_ADV_TYPE].handle &&
              app_ble_get_hdl_con_handle(con_adv_obj_hdl[APPLE_ADV_TYPE].handle)))
                ? 1
                : 0;

        // 如果有连接，则关闭可连接广播，开启不可连接广播
        if (has_conn)
        {
            for (i = 0; i < CON_ADV_OBJ_MAX_NUM; i++)
            {
                start_adv(&con_adv_obj_hdl[i], 0);
                start_adv(&no_con_adv_obj_hdl[i], 1);
            }
        }
        else
        {
            for (i = 0; i < CON_ADV_OBJ_MAX_NUM; i++)
            {
                start_adv(&no_con_adv_obj_hdl[i], 0);
                start_adv(&con_adv_obj_hdl[i], 1);
            }
        }
    }
    else // 关闭广播
    {
        for (i = 0; i < CON_ADV_OBJ_MAX_NUM; i++)
        {
            start_adv(&con_adv_obj_hdl[i], 0);
            start_adv(&no_con_adv_obj_hdl[i], 0);
        }
    }
    return 0;
}

/************************************************************************
**@brief: 开始广播对应的广播对象
**@param[in] adv_obj_hdl: 广播对象结构体指针
**@param[in] enable: 0:关闭,1:开启
*************************************************************************/
static void start_adv(ADV_HDL_S *adv_obj_hdl, u8 enable)
{
    uint8_t *advertising_data = NULL;
    uint8_t *scan_response_data = NULL;
    uint8_t adv_data_len = 0;
    uint8_t scan_rsp_data_len = 0;

    // 句柄为空说明对象尚未初始化，直接返回避免访问空指针
    if ((adv_obj_hdl == NULL) || (adv_obj_hdl->handle == NULL)) {
        return;
    }

    // 如果传入的adv_obj_hdl->valid为0，说明对应的对象句柄不需要广播
    if (adv_obj_hdl->valid == 0) {
        // printf("adv_obj_hdl->handle:%p,enable:%d", (int *)adv_obj_hdl->handle, enable);
        return;
    }

    // 如果当前广播句柄状态跟传入的enable状态一致，则不需要操作
    if (enable == app_ble_adv_state_get(adv_obj_hdl->handle)) {
        return ;
    }

    if (enable) {
        // 传入的句柄若是谷歌对象，则获取谷歌数据，否则获取ios数据
        if (adv_obj_hdl->handle == con_adv_obj_hdl[GOOGLE_ADV_TYPE].handle || adv_obj_hdl->handle == no_con_adv_obj_hdl[GOOGLE_ADV_TYPE].handle)
        {
            get_adv_data(GOOGLE_ADV_TYPE, &advertising_data, &adv_data_len, &scan_response_data, &scan_rsp_data_len);
        }
        else
        {
            get_adv_data(APPLE_ADV_TYPE, &advertising_data, &adv_data_len, &scan_response_data, &scan_rsp_data_len);
        }

        app_ble_adv_data_set(adv_obj_hdl->handle, advertising_data, adv_data_len);
        app_ble_rsp_data_set(adv_obj_hdl->handle, scan_response_data, scan_rsp_data_len);
    }
    app_ble_adv_enable(adv_obj_hdl->handle, enable);

    if (adv_obj_hdl->handle == con_adv_obj_hdl[GOOGLE_ADV_TYPE].handle)
    {
        printf("con_adv_obj_hdl[GOOGLE_ADV_TYPE]:%p, enable:%d", (int *)adv_obj_hdl->handle, enable);
    }
    else if (adv_obj_hdl->handle == con_adv_obj_hdl[APPLE_ADV_TYPE].handle)
    {
        printf("con_adv_obj_hdl[APPLE_ADV_TYPE]:%p, enable:%d", (int *)adv_obj_hdl->handle, enable);
    }
    else if (adv_obj_hdl->handle == no_con_adv_obj_hdl[GOOGLE_ADV_TYPE].handle)
    {
        printf("no_con_adv_obj_hdl[GOOGLE_ADV_TYPE]:%p, enable:%d", (int *)adv_obj_hdl->handle, enable);
    }
    else if (adv_obj_hdl->handle == no_con_adv_obj_hdl[APPLE_ADV_TYPE].handle)
    {
        printf("no_con_adv_obj_hdl[APPLE_ADV_TYPE]:%p, enable:%d", (int *)adv_obj_hdl->handle, enable);
    }
}

void ble_dc_power_off_handle(void)
{
    my_log_printf(1, "[DC_PWR] ble_dc_power_off_handle enter");
    g_ble_dc_power_suppressed = 1; // 先置抑制，防断链回调反向开广播
    // 清空链路状态，避免关机抑制期间误点亮图标
    s_ble_link = 0;
    s_bt_classic_link_cnt = 0;

    bt_ble_adv_enable(0);       // 关 BLE 广播
    my_findmy_ble_disconnect(); // 断 BLE GATT
    ble_dc_spp_disconnect();    // 断 SPP（若有）
    ble_dc_classic_shutdown();  // 断经典 BT + 关 scan/conn

    my_send_msg(MOD_MAIN, MOD_DC_UART, MY_MSG_DC_POLL_STOP); // 清 DC 蓝牙图标 Bit7
    my_send_msg(MOD_MAIN, MOD_BLE, MY_MSG_BLE_REPORT_STOP);  // 停 EMS 连接态上报
    ble_cancel_defer_ems_schedule();                         // 取消延后 EMS 调度
    ble_data_send_enable[GOOGLE_ADV_TYPE] = false;           // 清发送许可
    ble_data_send_enable[APPLE_ADV_TYPE] = false;
    ble_server_send_done = true; // 复位 GATT 发送状态
    ble_server_rx_index = 0;
    s_defer_ems_after_cid = 0;
    my_log_printf(1, "[DC_PWR] ble_dc_power_off_handle done");
}

void ble_dc_power_on_restore(void)
{
    my_log_printf(1, "[DC_PWR] ble_dc_power_on_restore enter");
    g_ble_dc_power_suppressed = 0; // 解除抑制，允许 connect/disconnect 与 dual_conn
    ble_dc_classic_restore();      // 重开经典 BT scan/page
    bt_ble_adv_enable(1);          // 重开 BLE 广播
    my_log_printf(1, "[DC_PWR] ble_dc_power_on_restore done");
}

void ble_dc_unbind_handle(void)
{
    my_log_printf(1, "[DC_UNBIND] ble_dc_unbind_handle enter");

#if TCFG_APP_BT_EN
    if (bt_get_curr_channel_state() != 0)
    {
        bt_cmd_prepare(USER_CTRL_POWER_OFF, 0, NULL); // 先断当前经典连接
    }
    bt_cmd_prepare(USER_CTRL_DEL_ALL_REMOTE_INFO, 0, NULL); // 清除全部配对记录
#endif

    my_findmy_ble_disconnect(); // 断 BLE GATT（FindMy 鉴权记录协议侧不擦除）
    ble_dc_spp_disconnect();

    if (!g_ble_dc_power_suppressed)
    {
        ble_dc_classic_restore(); // 恢复可被发现/连接，便于重新配对
        bt_ble_adv_enable(1);
    }

    play_tone_file(get_tone_files()->bt_unpaired);
    my_log_printf(1, "[DC_UNBIND] ble_dc_unbind_handle done");
}

void ble_connect_api(void)
{
    if (g_ble_dc_power_suppressed)
    {
        my_log_printf(1, "[DC_PWR] ignore ble_connect_api, suppressed=%u", (unsigned)g_ble_dc_power_suppressed);
        return;
    }

    // 记录BLE连接状态，并同步DC蓝牙图标
    s_ble_link = 1;
    dc_ble_icon_sync();
    printf("stop connect adv obj, start no_connect adv obj.");
    // 关闭两个可连接广播对象
    os_time_dly(10); 
    start_adv(&con_adv_obj_hdl[GOOGLE_ADV_TYPE], 0);
    os_time_dly(10); 
    start_adv(&con_adv_obj_hdl[APPLE_ADV_TYPE], 0);
    // 开启两个不可连接的广播对象
    os_time_dly(10); 
    start_adv(&no_con_adv_obj_hdl[GOOGLE_ADV_TYPE], 1);
    os_time_dly(10); 
    start_adv(&no_con_adv_obj_hdl[APPLE_ADV_TYPE], 1);
}

void ble_disconnect_api(void)
{
    if (g_ble_dc_power_suppressed)
    {
        my_log_printf(1, "[DC_PWR] ignore ble_disconnect_api, suppressed=%u", (unsigned)g_ble_dc_power_suppressed);
        return;
    }

    // 记录BLE断开状态，并同步DC蓝牙图标
    s_ble_link = 0;
    dc_ble_icon_sync();
    /* 断开时切到 BLE 线程统一收尾，把本轮的 RAM 清理提交到 VM。 */
    my_send_msg(MOD_MAIN, MOD_BLE, MY_MSG_BLE_REPORT_STOP);
    printf("stop no_connect adv obj, start connect adv obj.");
    // 断开ble连接时，设置发送蓝牙数据的标志为false，防止断开后还继续发送
    ble_data_send_enable[GOOGLE_ADV_TYPE] = false;
    ble_data_send_enable[APPLE_ADV_TYPE] = false;
    ble_server_send_done = true;
    ble_server_rx_index = 0;
    s_defer_ems_after_cid = 0; // 断开清defer
    // 关闭两个不可连接的广播对象
    start_adv(&no_con_adv_obj_hdl[GOOGLE_ADV_TYPE], 0);
    os_time_dly(10); 
    start_adv(&no_con_adv_obj_hdl[APPLE_ADV_TYPE], 0);
    // 开启两个可连接广播对象
    os_time_dly(10); 
    start_adv(&con_adv_obj_hdl[GOOGLE_ADV_TYPE], 1);
    os_time_dly(10); 
    start_adv(&con_adv_obj_hdl[APPLE_ADV_TYPE], 1);
}

void bt_connect_api(void)
{
    if (g_ble_dc_power_suppressed)
    {
        my_log_printf(1, "[DC_PWR] ignore bt_connect_api, suppressed=%u", (unsigned)g_ble_dc_power_suppressed);
        return;
    }

    // 经典BT连接数+1，支持双连；仅当全部断开才清图标
    if (s_bt_classic_link_cnt < 255)
    {
        s_bt_classic_link_cnt++;
    }

    // 经典BT连接后，同步DC蓝牙图标
    dc_ble_icon_sync();
}

void bt_disconnect_api(void)
{
    if (g_ble_dc_power_suppressed)
    {
        my_log_printf(1, "[DC_PWR] ignore bt_disconnect_api, suppressed=%u", (unsigned)g_ble_dc_power_suppressed);
        return;
    }

    // 经典BT连接数-1，支持双连；仅当全部断开才清图标
    if (s_bt_classic_link_cnt > 0)
    {
        s_bt_classic_link_cnt--;
    }

    // 经典BT断开后，同步DC蓝牙图标
    dc_ble_icon_sync();
}

static void custom_cbk_packet_handler(void *hdl, uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    u16 con_handle;
    int mtu;

    printf("cbk packet_type:0x%x, packet[0]:0x%x, packet[2]:0x%x", packet_type, packet[0], packet[2]);
    switch (packet_type)
    {
        case HCI_EVENT_PACKET:
            switch (hci_event_packet_get_type(packet)) 
            {
                case ATT_EVENT_CAN_SEND_NOW:
                    printf("ATT_EVENT_CAN_SEND_NOW");
                    ble_server_send_done = true;
                    my_send_msg(MOD_MAIN, MOD_BLE, MY_MSG_BLE_CAN_SEND_NOW);
                    break;

                case HCI_EVENT_LE_META:
                    switch (hci_event_le_meta_get_subevent_code(packet))
                    {
                        case HCI_SUBEVENT_LE_CONNECTION_COMPLETE:
                            // 对应广播对象被连接后，会自动关闭广播
                            con_handle = little_endian_read_16(packet, 4);
                            printf("HCI_SUBEVENT_LE_CONNECTION_COMPLETE: %0x", con_handle);
                            put_buf(&packet[8], 6);
                            // 发消息给消息队列去处理具体的事件
                            app_send_message(APP_MSG_BLE_CONNECTED, 0);
                            connect_id = con_handle;
                            printf("gap_connected! id=%d", connect_id);
                            break;

                        default:
                            break;
                    }
                    break;

                case HCI_EVENT_DISCONNECTION_COMPLETE:
                    connect_id = 0xff;
                    s_defer_ems_after_cid = 0;
                    printf("HCI_EVENT_DISCONNECTION_COMPLETE: %0x", packet[5]);
                    // 发消息给消息队列去处理具体的事件
                    app_send_message(APP_MSG_BLE_DISCONNECTED, 0);
                    break;

                case ATT_EVENT_MTU_EXCHANGE_COMPLETE:
                    mtu = att_event_mtu_exchange_complete_get_MTU(packet);
                    
                    printf("ATT MTU = %d", mtu);
                    ble_op_att_set_send_mtu(mtu - 3);

                    ble_server_mtu = mtu;
                    break;

                default:
                    break;
            }
            break;
    }
    return;
}

/**
 * @brief  从发送缓冲取数据经Notify发出
 * @return 0成功，非0失败
 */
static int ble_server_att_send_buffered(u16 len)
{
    if (ble_data_send_enable[GOOGLE_ADV_TYPE] == true)
    {
        return my_findmy_ble_google_send(uart_ble_server_buf, len);
    }

    if (ble_data_send_enable[APPLE_ADV_TYPE] == true)
    {
        return my_findmy_ble_ios_send(uart_ble_server_buf, len);
    }

    my_log_printf(1, "ble_data_send_enable not enable!");
    return -1;
}

/**
 * @brief  CAN_SEND_NOW时发送缓冲中的积压数据
 */
static void ble_server_flush_pending_tx(void)
{
    uint16_t tx_len;
    int ret;

    if (!ble_server_send_done || connect_id == 0xff || ble_server_rx_index == 0)
    {
        return; // 上一包未发完、已断开或缓冲空
    }

    // 按MTU取本包长度
    tx_len = ble_server_rx_index > MIN(BLE_SERVER_MAX_DATA_LEN, BLE_SVC_TX_MAX_LEN)
                 ? MIN(BLE_SERVER_MAX_DATA_LEN, BLE_SVC_TX_MAX_LEN)
                 : ble_server_rx_index;

    ret = ble_server_att_send_buffered(tx_len);
    if (ret != 0)
    {
        my_log_printf(1, "[BLE] notify send fail, keep buf len=%u", (unsigned)ble_server_rx_index); // 失败不挪buf
        return;
    }

    ble_server_send_done = false; // 等下次CAN_SEND_NOW
    ble_server_rx_index -= tx_len;
    my_log_printf(1, "ble_server_rx_index:%d", ble_server_rx_index);
    memmove(&uart_ble_server_buf[0], &uart_ble_server_buf[tx_len], ble_server_rx_index); // 前移剩余数据
}

/**
 * @brief  5505发完且发送空闲时启动EMS上报
 */
static void ble_server_try_schedule_deferred_ems(void)
{
    if (!s_defer_ems_after_cid)
    {
        return;
    }

    if (connect_id == 0xff || ble_server_rx_index != 0 || !ble_server_send_done)
    {
        return; // 还有待发或上一包未发完
    }

    s_defer_ems_after_cid = 0;
    my_log_printf(1, "[EMS] 5505 tx done, schedule connect reports");
    my_event_report_schedule();
}

void ble_server_on_can_send_now(void)
{
    ble_server_flush_pending_tx();
    ble_server_try_schedule_deferred_ems(); // flush后若5505已发完则拉EMS
}

/**
 * @brief  CID鉴权成功发5505后置defer，待5505发完再schedule EMS
 */
void ble_defer_ems_schedule_after_cid_auth(void)
{
    s_defer_ems_after_cid = 1;
    my_log_printf(1, "[EMS] defer schedule until 5505 tx done");
    ble_server_try_schedule_deferred_ems(); // 5505可能已发完，补试
}

/**
 * @brief  取消defer的EMS调度
 */
void ble_cancel_defer_ems_schedule(void)
{
    s_defer_ems_after_cid = 0;
}

/************************************************************************
**@brief: 蓝牙服务发送notify数据
**@param[in] data:      发送的数据
**@param[in] tx_len:    发送的数据长度
*************************************************************************/
void ble_server_send_notification(u8 *data, u16 tx_len)
{
    uint16_t tx_len_send;
    int ret;

    if(connect_id == 0xff)
    {
        my_log_printf(1, "ble send none in disconnect!");
        return;
    }

    // 未发送完成，先缓存起来
    if(ble_server_send_done == false)
    {
        if((tx_len > 0) && (ble_server_rx_index + tx_len) <= BLE_NOTIFY_SEND_BUF_MAX_SIZE)
        {
            memcpy(&uart_ble_server_buf[ble_server_rx_index], data, tx_len);
            ble_server_rx_index += tx_len;
        }
        my_log_printf(1, "ble_data_put_buf:%d, %d", ble_server_rx_index, tx_len);
    }
    else if(connect_id != 0xff && tx_len <= BLE_NOTIFY_SEND_BUF_MAX_SIZE)
    {
        if(tx_len > 0 && (ble_server_rx_index+tx_len) <= BLE_NOTIFY_SEND_BUF_MAX_SIZE)
        {
            memcpy(&uart_ble_server_buf[ble_server_rx_index], data, tx_len);
            ble_server_rx_index += tx_len;
        }

        tx_len_send = ble_server_rx_index > MIN(BLE_SERVER_MAX_DATA_LEN, BLE_SVC_TX_MAX_LEN)
                    ? MIN(BLE_SERVER_MAX_DATA_LEN, BLE_SVC_TX_MAX_LEN) : ble_server_rx_index;

        ret = ble_server_att_send_buffered(tx_len_send);
        if (ret != 0)
        {
            my_log_printf(1, "[BLE] notify send fail, keep buf len=%u", (unsigned)ble_server_rx_index); // 失败不挪buf
            return;
        }

        ble_server_send_done = false;
        ble_server_rx_index -= tx_len_send;
        my_log_printf(1, "ble_server_rx_index:%d", ble_server_rx_index);
        memmove(&uart_ble_server_buf[0], &uart_ble_server_buf[tx_len_send], ble_server_rx_index);
    }
}

// 暂未使用到这个回调，保留
static uint16_t custom_att_read_callback(void *hdl, hci_con_handle_t connection_handle, uint16_t att_handle, uint16_t offset, uint8_t *buffer, uint16_t buffer_size)
{
    uint16_t  att_value_len = 0;
    uint16_t handle = att_handle;
    printf("<-------------read_callback, handle= 0x%04x,buffer= %08x", handle, (u32)buffer);
    switch (handle)
    {
        case ATT_CHARACTERISTIC_2a00_01_VALUE_HANDLE:
            const char *gap_name = bt_get_local_name();
            att_value_len = strlen(gap_name);
            if ((offset >= att_value_len) || (offset + buffer_size) > att_value_len) {
                break;
            }
            if (buffer) {
                memcpy(buffer, &gap_name[offset], buffer_size);
                att_value_len = buffer_size;
                printf("\n------read gap_name: %s", gap_name);
            }
            break;

        case ATT_CHARACTERISTIC_ae02_01_CLIENT_CONFIGURATION_HANDLE:
            if (buffer) {
                buffer[0] = att_get_ccc_config(handle);
                buffer[1] = 0;
            }
            att_value_len = 2;
            break;

        default:
            break;
    }
    printf("att_value_len= %d", att_value_len);
    return att_value_len;
}

// APP开启监听FEB6值(需先设置att_set_ccc_config才能通过app_ble_att_send_data发送数据给APP)或APP下发数据会触发这个回调
static int custom_att_write_google_callback(void *hdl, hci_con_handle_t connection_handle, uint16_t att_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size)
{
    int result = 0;
    u16 tmp16;
    u16 handle = att_handle;
    printf("<-------------write_google_callback, handle= 0x%04x,size = %d", handle, buffer_size);

    switch (handle)
    {
        case ATT_CHARACTERISTIC_2a00_01_VALUE_HANDLE:
            break;
 
        case ATT_CHARACTERISTIC_ae01_01_VALUE_HANDLE:
            printf("rx(%d):\n", buffer_size);
            put_buf(buffer, buffer_size);

            BLE_DataInputBuffer(buffer, buffer_size);
            break;

        case ATT_CHARACTERISTIC_ae02_01_CLIENT_CONFIGURATION_HANDLE:
            ble_data_send_enable[GOOGLE_ADV_TYPE] = true;
            printf("\nwrite ccc:%04x, %02x\n", handle, buffer[0]);
            att_set_ccc_config(handle, buffer[0]);
            break;

        default:
            break;
    }
    return 0;
}

// 暂未使用到这个回调，保留
int my_findmy_ble_google_send(u8 *data, u32 len)
{
    int ret = 0;
    int i;

    if (data == NULL || len == 0) {
        my_log_printf(1, "invalid google params!!!");
        return -1;
    }

    my_log_printf(1, "my_findmy_ble_google_send len = %d", len);
    put_buf(data, len);
    ret = app_ble_att_send_data(con_adv_obj_hdl[GOOGLE_ADV_TYPE].handle, ATT_CHARACTERISTIC_ae02_01_VALUE_HANDLE, data, len, ATT_OP_AUTO_READ_CCC);
    if (ret) {
        my_log_printf(1, "send fail\n");
    }
    return ret;
}

// APP开启监听FEB6值(需先设置att_set_ccc_config才能通过app_ble_att_send_data发送数据给APP)或APP下发数据会触发这个回调
static int custom_att_write_ios_callback(void *hdl, hci_con_handle_t connection_handle, uint16_t att_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size)
{
    int result = 0;
    u16 tmp16;
    u16 handle = att_handle;
    printf("<-------------write_ios_callback, handle= 0x%04x,size = %d", handle, buffer_size);

    switch (handle)
    {
        case ATT_CHARACTERISTIC_2a00_01_VALUE_HANDLE:
            break;

        case ATT_CHARACTERISTIC_ae01_01_VALUE_HANDLE:
            printf("rx(%d):\n", buffer_size);
            put_buf(buffer, buffer_size);

            BLE_DataInputBuffer(buffer, buffer_size);
            break;

        case ATT_CHARACTERISTIC_ae02_01_CLIENT_CONFIGURATION_HANDLE:
            ble_data_send_enable[APPLE_ADV_TYPE] = true;
            printf("\nwrite ccc:%04x, %02x\n", handle, buffer[0]);
            att_set_ccc_config(handle, buffer[0]);
            break;

        default:
            break;
    }
    return 0;
}

// 暂未使用到这个回调，保留
int my_findmy_ble_ios_send(u8 *data, u32 len)
{
    int ret = 0;
    int i;

    if (data == NULL || len == 0) {
        my_log_printf(1, "invalid ios params!!!");
        return -1;
    }

    my_log_printf(1, "my_findmy_ble_ios_send len = %d", len);
    put_buf(data, len);
    ret = app_ble_att_send_data(con_adv_obj_hdl[APPLE_ADV_TYPE].handle, ATT_CHARACTERISTIC_ae02_01_VALUE_HANDLE, data, len, ATT_OP_AUTO_READ_CCC);
    if (ret) {
        my_log_printf(1, "send fail\n");
    }
    return ret;
}
/*************************************************
                  BLE 相关内容 end
*************************************************/

/*************************************************
                  SPP 相关内容(暂未使用到，保留)
*************************************************/
static void custom_spp_state_callback(void *hdl, void *remote_addr, u8 state)
{
    int i;
    int bond_flag = 0;
    switch (state)
    {
        case SPP_USER_ST_CONNECT:
            printf("custom spp connect#########\n");
            // 将 custom_demo_spp_hdl 绑定到连接上的设备地址，否则后续会收到所有已连接设备地址的事件和数据
            app_spp_set_filter_remote_addr(custom_demo_spp_hdl, remote_addr);
            break;

        case SPP_USER_ST_DISCONN:
            printf("custom spp disconnect#########\n");
            break;
    };
}

static void custom_spp_recieve_callback(void *hdl, void *remote_addr, u8 *buf, u16 len)
{
    printf("custom_spp_recieve_callback len=%d\n", len);
    put_buf(buf, len);

    custom_demo_spp_send(buf, len);
}

int custom_demo_spp_send(u8 *data, u32 len)
{
    return app_spp_data_send(custom_demo_spp_hdl, data, len);
}

/*************************************************
                  SPP 相关内容 end
*************************************************/

void my_findmy_init(void)
{
    int i;
    uint8_t adv_type;

    printf("my_findmy_init");
    const uint8_t *edr_addr = bt_get_mac_addr();
    printf("edr addr:");
    put_buf((uint8_t *)edr_addr, 6);

    // BLE init
    for (i = 0; i < CON_ADV_OBJ_MAX_NUM; i++)   //i=0为GOOGLE_ADV_TYPE，i=1为APPLE_ADV_TYPE，默认上电开启可连接广播对象
    {
        if (con_adv_obj_hdl[i].handle == NULL)
        {
            con_adv_obj_hdl[i].handle = app_ble_hdl_alloc();
            if (con_adv_obj_hdl[i].handle == NULL) {
                printf("con_adv_obj_hdl[%d].handle alloc err !", i);
                return;
            }
            app_ble_set_mac_addr(con_adv_obj_hdl[i].handle, (void *)edr_addr);
            app_ble_profile_set(con_adv_obj_hdl[i].handle, my_profile_data);
            app_ble_att_read_callback_register(con_adv_obj_hdl[i].handle, custom_att_read_callback);
            if (i == 0)
            {
                app_ble_att_write_callback_register(con_adv_obj_hdl[i].handle, custom_att_write_google_callback);
            }
            else
            {
                app_ble_att_write_callback_register(con_adv_obj_hdl[i].handle, custom_att_write_ios_callback);
            }
            app_ble_att_server_packet_handler_register(con_adv_obj_hdl[i].handle, custom_cbk_packet_handler);
            app_ble_hci_event_callback_register(con_adv_obj_hdl[i].handle, custom_cbk_packet_handler);
            app_ble_l2cap_packet_handler_register(con_adv_obj_hdl[i].handle, custom_cbk_packet_handler);

            adv_type = ADV_IND;
            app_ble_set_adv_param(con_adv_obj_hdl[i].handle, ADV_INTERVAL, adv_type, ADV_CHANNEL_ALL);
            start_adv(&con_adv_obj_hdl[i], 1);
        }

        if (no_con_adv_obj_hdl[i].handle == NULL)
        {
            no_con_adv_obj_hdl[i].handle = app_ble_hdl_alloc();
            if (no_con_adv_obj_hdl[i].handle == NULL) {
                printf("no_con_adv_obj_hdl[%d].handle alloc err !", i);
                return;
            }
            app_ble_set_mac_addr(no_con_adv_obj_hdl[i].handle, (void *)edr_addr);

            adv_type = ADV_SCAN_IND;
            app_ble_set_adv_param(no_con_adv_obj_hdl[i].handle, ADV_INTERVAL, adv_type, ADV_CHANNEL_ALL);

            start_adv(&no_con_adv_obj_hdl[i], 0);
        }
    }
    // BLE init end

    // SPP init
    if (custom_demo_spp_hdl == NULL) {
        custom_demo_spp_hdl = app_spp_hdl_alloc(0x0);
        if (custom_demo_spp_hdl == NULL) {
            printf("custom_demo_spp_hdl alloc err !");
            return;
        }
        app_spp_recieve_callback_register(custom_demo_spp_hdl, custom_spp_recieve_callback);
        app_spp_state_callback_register(custom_demo_spp_hdl, custom_spp_state_callback);
        app_spp_wakeup_callback_register(custom_demo_spp_hdl, NULL);
    }
    // SPP init end
}

void my_findmy_ble_disconnect(void)
{
    if (app_ble_get_hdl_con_handle(con_adv_obj_hdl[GOOGLE_ADV_TYPE].handle)) {
        app_ble_disconnect(con_adv_obj_hdl[GOOGLE_ADV_TYPE].handle);
    }

    if (app_ble_get_hdl_con_handle(con_adv_obj_hdl[APPLE_ADV_TYPE].handle)) {
        app_ble_disconnect(con_adv_obj_hdl[APPLE_ADV_TYPE].handle);
    }
}

void my_findmy_exit(void)
{
    printf("my_findmy_exit");

    // BLE exit
    if (app_ble_get_hdl_con_handle(con_adv_obj_hdl[GOOGLE_ADV_TYPE].handle)) {
        app_ble_disconnect(con_adv_obj_hdl[GOOGLE_ADV_TYPE].handle);
    }
    app_ble_hdl_free(con_adv_obj_hdl[GOOGLE_ADV_TYPE].handle);
    con_adv_obj_hdl[GOOGLE_ADV_TYPE].handle = NULL;

    if (app_ble_get_hdl_con_handle(con_adv_obj_hdl[APPLE_ADV_TYPE].handle)) {
        app_ble_disconnect(con_adv_obj_hdl[APPLE_ADV_TYPE].handle);
    }
    app_ble_hdl_free(con_adv_obj_hdl[APPLE_ADV_TYPE].handle);
    con_adv_obj_hdl[APPLE_ADV_TYPE].handle = NULL;

    app_ble_hdl_free(no_con_adv_obj_hdl[GOOGLE_ADV_TYPE].handle);
    no_con_adv_obj_hdl[GOOGLE_ADV_TYPE].handle = NULL;

    app_ble_hdl_free(no_con_adv_obj_hdl[APPLE_ADV_TYPE].handle);
    no_con_adv_obj_hdl[APPLE_ADV_TYPE].handle = NULL;

    // SPP exit
    if (NULL != app_spp_get_hdl_remote_addr(custom_demo_spp_hdl)) {
        app_spp_disconnect(custom_demo_spp_hdl);
    }
    app_spp_hdl_free(custom_demo_spp_hdl);
    custom_demo_spp_hdl = NULL;
}

#endif

