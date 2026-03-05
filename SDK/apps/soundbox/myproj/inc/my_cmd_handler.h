/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : my_cmd_handler.h
 * Brief      : 设备命令解析模块头文件
 * Version    : V1.0.0
 * Author     : 周森达(zhousenda@jimiiot.com)
 * Date       : 2026-03-04
************************************************************************************************/
#ifndef _MY_CMD_HANDLER_H_
#define _MY_CMD_HANDLER_H_

#define AT_CMD_TABLE_TOTAL (sizeof(at_cmd_attr_table)/sizeof(at_cmd_attr_t))

#define RESP_STRING_LENGTH_MAX              BLE_SVC_RX_MAX_LEN
#define CMD_STRING_LENGTH_MAX               128                 //暂定可接收cmd缓冲区大小

typedef enum
{
    PARM_1,
    PARM_2,
    PARM_3,
    PARM_4,
    PARM_5,
    PARM_6,
    PARM_7,
    PARM_8,
    PARM_MAX
}cmd_parm_struct;

typedef struct {
    char *parm[PARM_MAX];
    uint8 parm_count;

    char rcv_msg[CMD_STRING_LENGTH_MAX];            //接收到的数据
    uint16 rcv_length;

    char resp_msg[RESP_STRING_LENGTH_MAX];          //应答数据
    uint16 resp_length;
} at_cmd_struc;

typedef int(*at_cmd_handler_t)(at_cmd_struc *msg);

typedef struct
{
    char *cmd_str;
    at_cmd_handler_t cmd_func;
} at_cmd_attr_t;

void my_send_all_status_handle(void);
uint16 at_recv_cmd_handler(at_cmd_struc *at_cmd_msg);

#endif