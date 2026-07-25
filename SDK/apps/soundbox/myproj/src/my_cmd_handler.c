/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : my_cmd_handler.c
 * Brief      : 设备命令解析模块
 * Version    : V1.0.0
 * Author     : 周森达(zhousenda@jimiiot.com)
 * Date       : 2026-03-04
************************************************************************************************/
#include "my_common.h"

extern void multi_protocol_rcsp_adv_ota_mode_set(u8 enable);

static int mb300_sw_cmd_handler(at_cmd_struc* msg);
static int mb300_ota_mode_handler(at_cmd_struc* msg);
static int mb300_get_status_handler(at_cmd_struc* msg);
static int mb300_my_dc_ota_handler(at_cmd_struc* msg);

static const at_cmd_attr_t at_cmd_attr_table[] =
{
    {"MB300_SW_AC",         mb300_sw_cmd_handler},
    {"MB300_SW_DC",         mb300_sw_cmd_handler},
    {"MB300_SW_USB",        mb300_sw_cmd_handler},
    {"MB300_SW_LED",        mb300_sw_cmd_handler},
    {"MB300_SW_OTA",        mb300_ota_mode_handler},
    {"MB300_DC_OTA",        mb300_my_dc_ota_handler},
    {"MB300_GET_STATUS",    mb300_get_status_handler},
};

/************************************************************************
**@brief: 打包单个模块的数据(包含模块ID、数据长度、数据内容)
**@param[in] out_buf: 输出缓冲区，存储打包后的数据
**@param[in] buf_size: 输出缓冲区的总大小
**@param[inout] offset: 缓冲区写入偏移量，入参为当前偏移，出参为写入后偏移
**@param[in] id: 模块ID(单字节/双字节)
**@param[in] data: 待打包的原始数据
**@param[in] len: 待打包数据的长度
**@return: 0-打包成功, -1-打包失败(参数异常/缓冲区不足)
*************************************************************************/
static int32 pack_single_module(uint8 *out_buf, uint16 buf_size, uint16 *offset, 
                                  uint16 id, const uint8 *data, uint8 len)
{
    uint16 need_size;

    // 参数合法性检查
    if (out_buf == NULL || buf_size ==0 || offset == NULL || data == NULL || len == 0) {
        return -1;
    }

    // 检查缓冲区剩余空间是否足够（ID长度+len长度+数据长度）
    need_size = (id > 0xFF) ? (2 + 1 + len) : (1 + 1 + len); // 双字节ID占2字节，单字节占1字节
    if ((*offset + need_size) > buf_size) {
        return -1;
    }

    // 写入模块ID（双字节拆分为高+低字节，单字节直接写）
    if (id > 0xFF) {
        out_buf[*offset] = (uint8)(id >> 8);   // 高字节
        (*offset)++;
        out_buf[*offset] = (uint8)(id & 0xFF); // 低字节
        (*offset)++;
    } else {
        out_buf[*offset] = (uint8)id;          // 单字节ID
        (*offset)++;
    }

    // 写入数据长度
    out_buf[*offset] = len;
    (*offset)++;

    // 写入数据内容
    memcpy(&out_buf[*offset], data, len);
    (*offset) += len;

    return 0;
}

/************************************************************************
**@brief: 打包整个设备的状态数据报文(整合所有模块数据，添加长度头)
**@param[in] out_buf: 输出缓冲区，存储最终打包的设备状态报文
**@param[in] buf_size: 输出缓冲区的总大小
**@param[in] dev_data: 设备状态数据结构体指针，包含所有待打包的状态字段
**@return: 成功返回打包后报文总长度，失败返回-1
*************************************************************************/
uint16 pack_device_data_packet(uint8 *out_buf, uint16 buf_size, const device_data *dev_data)
{
    int32 ret = 0;
    uint16 offset = 1;    // 数组写入偏移量
    uint16 temp_16;       // 临时存储大端序16位数据
    uint8 data_len;

    // 1. 参数合法性检查
    if (out_buf == NULL || buf_size == 0 || dev_data == NULL) {
        return -1;
    }

    // 2. 依次封装各字段（调用封装函数，数据转换为大端序）
    // 2.1 电量百分比（单字节ID，单字节数据）
    ret = pack_single_module(out_buf, buf_size, &offset, BAT_PERCENT, &dev_data->bat_percent, sizeof(dev_data->bat_percent));
    if (ret != 0) return -1;

    // 2.2 剩余时长（双字节ID，双字节数据，大端序）
    data_len = sizeof(dev_data->remain_time);
    swapX(&dev_data->remain_time, &temp_16, data_len);
    ret = pack_single_module(out_buf, buf_size, &offset, REMAIN_TIME, (uint8*)&temp_16, data_len);
    if (ret != 0) return -1;

    // 2.3 电池温度（双字节ID，双字节数据，大端序）
    data_len = sizeof(dev_data->bat_temp);
    swapX(&dev_data->bat_temp, &temp_16, data_len);
    ret = pack_single_module(out_buf, buf_size, &offset, BAT_TEMP, (uint8*)&temp_16, data_len);
    if (ret != 0) return -1;

    // 2.4 输出总功率（双字节ID，双字节数据，大端序）
    data_len = sizeof(dev_data->output_total_power);
    swapX(&dev_data->output_total_power, &temp_16, data_len);
    ret = pack_single_module(out_buf, buf_size, &offset, OUTPUT_TOTAL_POWER, (uint8*)&temp_16, data_len);
    if (ret != 0) return -1;

    // 2.5 输入总功率（双字节ID，双字节数据，大端序）
    data_len = sizeof(dev_data->input_total_power);
    swapX(&dev_data->input_total_power, &temp_16, data_len);
    ret = pack_single_module(out_buf, buf_size, &offset, INPUT_TOTAL_POWER, (uint8*)&temp_16, data_len);
    if (ret != 0) return -1;

    // 2.6 AC功率（双字节ID，双字节数据，大端序）
    data_len = sizeof(dev_data->ac_power);
    swapX(&dev_data->ac_power, &temp_16, data_len);
    ret = pack_single_module(out_buf, buf_size, &offset, AC_POWER, (uint8*)&temp_16, data_len);
    if (ret != 0) return -1;

    // 2.7 DC功率（双字节ID，双字节数据，大端序）
    data_len = sizeof(dev_data->dc_power);
    swapX(&dev_data->dc_power, &temp_16, data_len);
    ret = pack_single_module(out_buf, buf_size, &offset, DC_POWER, (uint8*)&temp_16, data_len);
    if (ret != 0) return -1;

    // 2.8 USB功率（双字节ID，双字节数据，大端序）
    data_len = sizeof(dev_data->usb_power);
    swapX(&dev_data->usb_power, &temp_16, data_len);
    ret = pack_single_module(out_buf, buf_size, &offset, USB_POWER, (uint8*)&temp_16, data_len);
    if (ret != 0) return -1;

    // 2.9 LED功率（双字节ID，双字节数据，大端序）
    data_len = sizeof(dev_data->led_power);
    swapX(&dev_data->led_power, &temp_16, data_len);
    ret = pack_single_module(out_buf, buf_size, &offset, LED_POWER, (uint8*)&temp_16, data_len);
    if (ret != 0) return -1;

    // 2.10 开关状态（双字节ID，单字节数据）
    ret = pack_single_module(out_buf, buf_size, &offset, SW_STATUS, &dev_data->sw_status, sizeof(dev_data->sw_status));
    if (ret != 0) return -1;

    // 2.11 故障状态（双字节ID，单字节数据）
    ret = pack_single_module(out_buf, buf_size, &offset, FAULT_STATUS, &dev_data->fault_status, sizeof(dev_data->fault_status));
    if (ret != 0) return -1;

    // 第一个index存放后面的所有数据的长度(不包括本身)
    out_buf[0] = (offset-1);

    // 3. 返回封装后的总长度
    return offset;
}

/************************************************************************
**@brief: 处理MB300设备状态获取指令(MB300_GET_STATUS)
**@param[in] msg: AT指令结构体指针，包含指令参数和响应缓冲区信息
**@return: 返回BLE数据类型（扩展模块数据类型）
*************************************************************************/
static int mb300_get_status_handler(at_cmd_struc* msg)
{
    device_data dev = {0};

    // 检查 DC 是否在线 或 获取数据失败
    if (!my_dc_is_online() || my_dc_get_data(&dev) != 0)
    {
        msg->resp_length = 0;
        return BLE_DATA_TYPE_EXPANSION_MODULE;
    }

    msg->resp_length = pack_device_data_packet((uint8 *)msg->resp_msg, sizeof(msg->resp_msg), &dev);
    return BLE_DATA_TYPE_EXPANSION_MODULE;
}

/* MB300_SW_LED 模式字符串 -> Modbus Bit4~6 模式值 */
static int mb300_led_mode_from_str(const char *mode_str, uint8 *out_mode)
{
    if (mode_str == NULL || out_mode == NULL)
    {
        return -1;
    }

    if (!strcmp(mode_str, "000"))
    {
        *out_mode = 0;
    }
    else if (!strcmp(mode_str, "001"))
    {
        *out_mode = 1;
    }
    else if (!strcmp(mode_str, "010"))
    {
        *out_mode = 2;
    }
    else if (!strcmp(mode_str, "011"))
    {
        *out_mode = 3;
    }
    else if (!strcmp(mode_str, "100"))
    {
        *out_mode = 4;
    }
    else
    {
        return -1;
    }

    return 0;
}

/************************************************************************
**@brief: 处理MB300设备开关控制指令(AC/DC/USB/LED)
**@param[in] msg: AT指令结构体指针，包含指令参数和响应缓冲区信息
**@return: 返回BLE数据类型（AT指令响应类型）
*************************************************************************/
static int mb300_sw_cmd_handler(at_cmd_struc* msg)
{
    my_dc_sw_id_t sw_id = MY_DC_SW_AC;
    uint8 sw_on = 0;
    uint16 remaining = sizeof(msg->resp_msg);

    if(msg->parm_count == 1)
    {
        my_log_printf(1, "%s=>%s,%s", __func__, msg->parm[0], msg->parm[1]);
        if (!strcmp(msg->parm[0], "MB300_SW_AC"))
        {
            sw_id = MY_DC_SW_AC;
        }
        else if (!strcmp(msg->parm[0], "MB300_SW_DC"))
        {
            sw_id = MY_DC_SW_DC;
        }
        else if (!strcmp(msg->parm[0], "MB300_SW_USB"))
        {
            sw_id = MY_DC_SW_USB;
        }
        else if (!strcmp(msg->parm[0], "MB300_SW_LED"))
        {
            sw_id = MY_DC_SW_LED;
        }
        else
        {
            msg->resp_length = snprintf(msg->resp_msg, remaining, "RETURN_%s_FAIL", msg->parm[0]);
            return BLE_DATA_TYPE_AT_CMD;
        }

        if (sw_id == MY_DC_SW_LED)
        {
            if (mb300_led_mode_from_str(msg->parm[1], &sw_on) != 0)
            {
                msg->resp_length = snprintf(msg->resp_msg, remaining, "RETURN_%s_%s_FAIL", msg->parm[0], msg->parm[1]);
                return BLE_DATA_TYPE_AT_CMD;
            }
        }
        else
        {
            if (!strcmp(msg->parm[1], "ON"))
            {
                sw_on = 1;
            }
            else if (!strcmp(msg->parm[1], "OFF"))
            {
                sw_on = 0;
            }
            else
            {
                msg->resp_length = snprintf(msg->resp_msg, remaining, "RETURN_%s_%s_FAIL", msg->parm[0], msg->parm[1]);
                return BLE_DATA_TYPE_AT_CMD;
            }
        }

        if (my_dc_ctrl_switch_with_ack(sw_id, sw_on, msg->parm[0], msg->parm[1]) != 0)
        {
            msg->resp_length = snprintf(msg->resp_msg, remaining, "RETURN_%s_%s_FAIL", msg->parm[0], msg->parm[1]);
            return BLE_DATA_TYPE_AT_CMD;
        }

        /* 异步模式：ACK 由 DC 模块定时器推送，resp_length=0 告知上层勿发 BLE 响应 */
        msg->resp_length = 0;
        return 0;
    }
    else
    {
        my_log_printf(1, "%s=>%s", __func__, msg->parm[0]);
        msg->resp_length = snprintf(msg->resp_msg, remaining, "RETURN_%s_FAIL", msg->parm[0]);
    }

    return BLE_DATA_TYPE_AT_CMD;
}

/************************************************************************
**@brief: 处理OTA模式切换指令(MB300_SW_OTA,ON/OFF)
**@param[in] msg: AT指令结构体指针，包含指令参数和响应缓冲区信息
**@return: 返回BLE数据类型（AT指令响应类型）
*************************************************************************/
static int mb300_ota_mode_handler(at_cmd_struc *msg)
{
    uint8 ota_en = 0;
    uint16 remaining = sizeof(msg->resp_msg);

    if (msg->parm_count != 1)
    {
        msg->resp_length = snprintf(msg->resp_msg, remaining, "RETURN_%s_FAIL", msg->parm[0]);
        return BLE_DATA_TYPE_AT_CMD;
    }

    my_log_printf(1, "%s=>%s,%s", __func__, msg->parm[0], msg->parm[1]);

    if (!strcmp(msg->parm[1], "ON"))
    {
        ota_en = 1;
        my_dc_switch_bit_set(DC_REG_SWITCH_BIT_BT_OTA, 1);
        my_log_printf(1, "[DC_OTA] stop dc uart poll");
        my_dc_uart_deinit(); // 切换广播后，停止查询数据
    }
    else if (!strcmp(msg->parm[1], "OFF"))
    {
        ota_en = 0;
        my_log_printf(1, "[DC_OTA] restart dc uart poll");
        my_dc_uart_init(); // 切换广播后，重新查询数据
        my_dc_switch_bit_set(DC_REG_SWITCH_BIT_BT_OTA, 0);
    }
    else
    {
        msg->resp_length = snprintf(msg->resp_msg, remaining, "RETURN_%s_%s_FAIL", msg->parm[0], msg->parm[1]);
        return BLE_DATA_TYPE_AT_CMD;
    }

    /*
     * 生效时机说明：
     * 1) 本指令只切换“后续广播策略”，不会强制断开当前BLE连接。
     * 2) 若APP当前已连在FEE5(JIMI协议)通道，设置ON后本次连接仍保持FEE5可继续通信。
     * 3) 需要APP主动断开并再次连接，新的连接才会命中AE00(RCSP协议)用于OTA流程。
     */
    multi_protocol_rcsp_adv_ota_mode_set(ota_en);
    msg->resp_length = snprintf(msg->resp_msg, remaining, "RETURN_%s_%s_OK", msg->parm[0], msg->parm[1]);
    return BLE_DATA_TYPE_AT_CMD;
}

/************************************************************************
**@brief: 处理DC OTA指令(MB300_DC_OTA)
**@param[in] msg: AT指令结构体指针，包含指令参数和响应缓冲区信息
**@return: 返回BLE数据类型（AT指令响应类型）
*************************************************************************/
static int mb300_my_dc_ota_handler(at_cmd_struc *msg)
{
    uint16 remaining = sizeof(msg->resp_msg);

    if (my_dc_ota_request_start() != 0)
    {
        msg->resp_length = snprintf(msg->resp_msg, remaining,
                                    "RETURN_MB300_DC_OTA_START_FAIL");
        return BLE_DATA_TYPE_AT_CMD;
    }

    msg->resp_length = snprintf(msg->resp_msg, remaining,
                                "RETURN_MB300_DC_OTA_START_OK");
    return BLE_DATA_TYPE_AT_CMD;
}

/************************************************************************
**@brief: 解析AT指令字符串，拆分参数到指定数组
**@param[in] str_data: 待解析的AT指令字符串
**@param[out] tar_data: 输出参数数组，存储拆分后的指令参数
**@param[in] limit: 参数数组最大长度（限制拆分数量）
**@param[in] startChar: 指令起始字符（NULL表示无起始字符）
**@param[in] endChars: 指令结束字符集（如"\r\n"）
**@param[in] splitChar: 参数分隔字符（如','）
**@return: 成功返回实际拆分的参数数量，失败返回-1(入参异常)/-2(超上限)/-3(分隔符后超上限)
*************************************************************************/
int at_cmd_str_analyse(char *str_data, char **tar_data, int limit, char startChar, char *endChars, char splitChar)
{
    static char *blank = "";
    int len, i = 0, j = 0, status = 0;
    char *p;

    if(str_data == NULL)
    {
        return -1;
    }
    len = strlen(str_data);
    for(i = 0, j = 0, p = str_data; i < len; i++, p++)
    {
        if(status == 0 && (*p == startChar || startChar == NULL))
        {
            status = 1;
            if(j >= limit)
            {
                return -2;
            }
            if(startChar == NULL)
            {
                tar_data[j++] = p;
            }
            else if(*(p + 1) == splitChar)
            {
                tar_data[j++] = blank;
            }
            else
            {
                tar_data[j++] = p + 1;
            }
        }
        if(status == 0)
        {
            continue;
        }
        if(strchr(endChars, *p) != NULL)
        {
            *p = 0;
            break;
        }
        if(*p == splitChar)
        {
            *p = 0;
            if(j >= limit)
            {
                return -3;
            }
            if(strchr(endChars, *(p + 1)) != NULL || *(p + 1) == splitChar)
            {
                tar_data[j++] = blank;
            }
            else
            {
                tar_data[j++] = p + 1;
            }
        }
    }
    for(i = j; i < limit; i++)
    {
        tar_data[i] = blank;
    }
    return j;
}

/************************************************************************
**@brief: 解析AT指令并执行对应的处理函数
**@param[inout] at_cmd_msg: AT指令结构体指针，包含接收的指令和响应存储区域
**@return: 成功返回处理函数返回的BLE数据类型，未匹配指令返回0
*************************************************************************/
uint16 at_recv_cmd_handler(at_cmd_struc *at_cmd_msg)
{
    char   *data_ptr, split_ch = ',';
    uint8  cmd_Len, par_len;
    uint16 cmd_type = 0;
    uint8 index;

    data_ptr = at_cmd_msg->rcv_msg;
    cmd_Len = strlen(data_ptr);
    data_ptr[cmd_Len] = '\r';
    data_ptr[cmd_Len+1] = '\n';

    // 解析AT指令参数
    par_len = at_cmd_str_analyse(data_ptr, at_cmd_msg->parm, PARM_MAX, NULL, "\r\n", split_ch);
    if(par_len > PARM_MAX || par_len <= 0)
    {
        my_log_printf(1, "at_cmd_analyse_par_len error, len=%d", par_len);
        return cmd_type;
    }

    at_cmd_msg->parm_count = par_len - 1;
#if 0
    if(at_cmd_msg->parm_count)
    {
        my_log_printf(1, "recv_cmd:par_num=%d,%s,%s", at_cmd_msg->parm_count, at_cmd_msg->parm[PARM_1], at_cmd_msg->parm[PARM_2]);
    }
    else
    {
        my_log_printf(1, "recv_cmd:par_num=%d,%s", at_cmd_msg->parm_count, at_cmd_msg->parm[PARM_1]);
    }
#endif
    // 匹配指令并执行对应处理函数
    for (index = 0; index < AT_CMD_TABLE_TOTAL; index++)
    {
        if (strcmp(at_cmd_attr_table[index].cmd_str, at_cmd_msg->parm[PARM_1]) == 0)
        {
            if (at_cmd_attr_table[index].cmd_func != NULL)
            {
                cmd_type = at_cmd_attr_table[index].cmd_func(at_cmd_msg);
            }
            break;
        }
    }
    return cmd_type;
}

/************************************************************************
**@brief: 主动发送设备所有状态数据到APP端
**@param: 无
**@return: 无
*************************************************************************/
void my_send_all_status_handle(void)
{
    at_cmd_struc ble_at_msg={0};
    uint16 cmd_type = 0;

    // 检查BLE连接状态
    if (check_connect_id_enable() == 0) {
        my_log_printf(1, "ble not connected, send fail.");
        return ;
    }

    // 打包设备状态数据
    cmd_type = mb300_get_status_handler(&ble_at_msg);
    my_log_printf(1, "ble_at_msg.resp_length:%d", ble_at_msg.resp_length);

    // 校验长度并发送数据
    if(ble_at_msg.resp_length > 0 && ble_at_msg.resp_length <= (BLE_SERVER_MAX_DATA_LEN - 4))
    {
        ble_comu_response_or_expansion_cmd(cmd_type, (uint8*)ble_at_msg.resp_msg, ble_at_msg.resp_length);
    }
}