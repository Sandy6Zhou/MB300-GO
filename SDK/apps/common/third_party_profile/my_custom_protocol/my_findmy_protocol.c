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
#include "my_common.h"
#include "my_findmy_protocol.h"

#if (THIRD_PARTY_PROTOCOLS_SEL & CUSTOM_DEMO_EN && (MY_FINDMY_EN == 1))

#define DEV_CUST_UUID       0xFEE5  // 自定义UUID
#define FLAG_TYPE_VALUE     0x06    // google的flag值
#define CON_ADV_OBJ_MAX_NUM 2       // 一路google，一路ios
#define ADV_INTERVAL        3200    // 3200*0.625ms=2000ms
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

static bool ble_server_send_done = true;
static bool ble_data_send_enable[2] = {0};

static void start_adv(ADV_HDL_S *adv_obj_hdl, u8 enable);
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

void ble_connect_api(void)
{
    my_send_msg(MOD_MAIN, MOD_DC_UART, MY_MSG_DC_POLL_START);  /* 蓝牙APP连接：启动 DC 轮询定时器 */
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
    my_send_msg(MOD_MAIN, MOD_DC_UART, MY_MSG_DC_POLL_STOP);   /* 蓝牙APP断开：完全停止 DC 轮询定时器 */
    printf("stop no_connect adv obj, start connect adv obj.");
    // 断开ble连接时，设置发送蓝牙数据的标志为false，防止断开后还继续发送
    ble_data_send_enable[GOOGLE_ADV_TYPE] = false;
    ble_data_send_enable[APPLE_ADV_TYPE] = false;
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

/************************************************************************
**@brief: 蓝牙服务发送notify数据
**@param[in] data:      发送的数据
**@param[in] tx_len:    发送的数据长度
*************************************************************************/
void ble_server_send_notification(u8 *data, u16 tx_len)
{
    static uint8_t uart_ble_server_buf[BLE_NOTIFY_SEND_BUF_MAX_SIZE];
    uint16_t _tx_len;

    if(connect_id == 0xff)
    {
        printf("ble send none in disconnect!");
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
        printf("ble_data_put_buf:%d, %d", ble_server_rx_index, tx_len);
    }
    else if(connect_id != 0xff && tx_len <= BLE_NOTIFY_SEND_BUF_MAX_SIZE)
    {
        ble_server_send_done = false;
        if(tx_len > 0 && (ble_server_rx_index+tx_len) <= BLE_NOTIFY_SEND_BUF_MAX_SIZE)
        {
            memcpy(&uart_ble_server_buf[ble_server_rx_index], data, tx_len);
            ble_server_rx_index += tx_len;
        }

        _tx_len = ble_server_rx_index > MIN(BLE_SERVER_MAX_DATA_LEN, BLE_SVC_TX_MAX_LEN) ? MIN(BLE_SERVER_MAX_DATA_LEN, BLE_SVC_TX_MAX_LEN) : ble_server_rx_index;

        if (ble_data_send_enable[GOOGLE_ADV_TYPE] == true) {
            my_findmy_ble_google_send(uart_ble_server_buf, _tx_len);
        } else if (ble_data_send_enable[APPLE_ADV_TYPE] == true) {
            my_findmy_ble_ios_send(uart_ble_server_buf, _tx_len);
        } else {
            printf("ble_data_send_enable not enable!");
            ble_server_send_done = true;
            return ;
        }

        ble_server_rx_index -= _tx_len;
        printf("ble_server_rx_index:%d", ble_server_rx_index);

        memcpy(&uart_ble_server_buf[0], &uart_ble_server_buf[_tx_len], ble_server_rx_index);
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
        printf("invalid google params!!!");
        return -1;
    }

    printf("my_findmy_ble_google_send len = %d", len);
    put_buf(data, len);
    ret = app_ble_att_send_data(con_adv_obj_hdl[GOOGLE_ADV_TYPE].handle, ATT_CHARACTERISTIC_ae02_01_VALUE_HANDLE, data, len, ATT_OP_AUTO_READ_CCC);
    if (ret) {
        printf("send fail\n");
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
        printf("invalid ios params!!!");
        return -1;
    }

    printf("my_findmy_ble_ios_send len = %d", len);
    put_buf(data, len);
    ret = app_ble_att_send_data(con_adv_obj_hdl[APPLE_ADV_TYPE].handle, ATT_CHARACTERISTIC_ae02_01_VALUE_HANDLE, data, len, ATT_OP_AUTO_READ_CCC);
    if (ret) {
        printf("send fail\n");
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

