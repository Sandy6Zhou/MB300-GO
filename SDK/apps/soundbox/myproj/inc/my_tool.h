/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : my_tool.h
 * Brief      : 设备平台工具模块头文件
 * Version    : V1.0.0
 * Author     : 黄俊凯(huangjunkai@jimiiot.com)
 * Date       : 2025-12-12
************************************************************************************************/

#ifndef MY_TOOL_H
#define MY_TOOL_H

typedef void (*TIMER_FUN)(void *param);

typedef struct
{
    uint32 msgID;
    void* pData;
    uint32 DataLen;
} MSG_S;

int my_start_timer(int timerId, uint32 ms, bool isPeriod, TIMER_FUN timer_fun);

void my_stop_timer(int timerId);

void my_send_msg(module_type src_mod_id, module_type dest_mod_id, uint32 msg);

void my_send_msg_data(module_type src_mod_id, module_type dest_mod_id, MSG_S *msg);

void my_get_os_cpu_usage(void);

uint8 string_check_is_hex_str(const char* str);

uint8 hexstr_to_hex(uint8 *dest, uint8 dest_size, const char *src);

#endif