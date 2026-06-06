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

#ifndef MAX
#define MAX(a, b)       (((a) > (b) ) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b)       (((a) < (b) ) ? (a) : (b))
#endif
#ifndef MID
#define MID(a,b,c)      (MAX(a,b)>c?MAX(MIN(a,b),c):MIN(MAX(a,b),c))
#endif

#ifndef ABS
#define ABS(x)  ((x) >= 0 ? (x) : -(x))
#endif

#ifndef my_swap16
#define my_swap16(s)  (uint16)((((uint16)(s) & 0x00FFu) << 8) | ((((uint16)(s) >> 8) & 0x00FFu)))
#endif

#ifndef my_swap32
#define my_swap32(l)  (uint32)(((((uint32)(l) >> 24) & 0x000000FFu)) | \
                               ((((uint32)(l) & 0x00FF0000u) >> 8)) | \
                               ((((uint32)(l) & 0x0000FF00u) << 8)) | \
                               ((((uint32)(l) & 0x000000FFu) << 24)))
#endif

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

void hex2hexstr(const uint8 *hex, uint16 hex_len, uint8 *str, uint16 str_len);

uint8 string_check_is_number(uint8 flag, const char* str);

#endif
