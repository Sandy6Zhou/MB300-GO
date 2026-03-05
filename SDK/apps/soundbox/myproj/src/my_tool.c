/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : my_tool.c
 * Brief      : 设备平台工具模块
 * Version    : V1.0.0
 * Author     : 黄俊凯(huangjunkai@jimiiot.com)
 * Date       : 2025-12-12
************************************************************************************************/

#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".my_tool.data.bss")
#pragma data_seg(".my_tool.data")
#pragma const_seg(".my_tool.text.const")
#pragma code_seg(".my_tool.text")
#endif

#include "my_common.h"

my_timer_t g_my_timer_info[MY_TIMER_MAX_ID] = { 0 };

/************************************************************************
**@brief: 启动系统定时器
**@param[in] timerId: 定时器ID
**@param[in] ms: 定时时间
**@param[in] isPeriod: 是否会循环定时
**@param[in] timer_fun: 定时任务处理函数
*************************************************************************/
int my_start_timer(int timerId, uint32 ms, bool isPeriod, TIMER_FUN timer_fun)
{
    if (g_my_timer_info[timerId].id != 0)
    {
        // sys_timer_del、sys_timeout_del需要和sys_timer_add、sys_timeout_add
        // 成对使用，所以要先判断用的是哪个接口添加的定时任务
        if (g_my_timer_info[timerId].isPeriod) {
            sys_timer_del(g_my_timer_info[timerId].id);
        }
        else {
            sys_timeout_del(g_my_timer_info[timerId].id);
        }

        memset(&g_my_timer_info[timerId], 0, sizeof(my_timer_t));
    }

    if (isPeriod)
    {
        // 添加系统非timeout类型定时任务
        // 与sys_timer_del成对使用
        g_my_timer_info[timerId].id = sys_timer_add(NULL, timer_fun, ms);
    }
    else
    {
        // 添加ms级系统timeout类型定时任务
        // timeout回调只会被执行一次
        // 与sys_timerout_del成对使用
        g_my_timer_info[timerId].id = sys_timeout_add(NULL, timer_fun, ms);
    }


    if (g_my_timer_info[timerId].id == 0)
    {
        my_log_printf(1, "\r\nFailed to Add System Timer ID [%d]\r\n", timerId);
        return 0;
    }
    g_my_timer_info[timerId].isPeriod = isPeriod;

    return 1;
}

/************************************************************************
**@brief: 停止系统定时器
**@param[in] timerId: 定时器ID
*************************************************************************/
void my_stop_timer(int timerId)
{
    if (g_my_timer_info[timerId].id != 0)
    {
        if (g_my_timer_info[timerId].isPeriod) {
            sys_timer_del(g_my_timer_info[timerId].id);
        }
        else {
            sys_timeout_del(g_my_timer_info[timerId].id);
        }

        memset(&g_my_timer_info[timerId], 0, sizeof(my_timer_t));
    }
}

/************************************************************************
**@brief: 向指定模块发送消息
**@param[in] src_mod_id: 发送消息的源模块ID
**@param[in] dest_mod_id: 接收消息的目标模块ID
**@param[in] msg: 消息信息
*************************************************************************/
void my_send_msg(module_type src_mod_id, module_type dest_mod_id, uint32 msg)
{
    const char *mod_id = g_my_task_info[dest_mod_id];

    os_taskq_post_type(mod_id, msg, 0, NULL);
}

/************************************************************************
**@brief: 向指定模块发送消息
**@param[in] src_mod_id: 发送消息的源模块ID
**@param[in] dest_mod_id: 接收消息的目标模块ID
**@param[in] msg: 消息信息
*************************************************************************/
void my_send_msg_data(module_type src_mod_id, module_type dest_mod_id, MSG_S *msg)
{
    const char *mod_id = g_my_task_info[dest_mod_id];
    // NOTE: 参数个数需要按int类型来对齐
    int param_num = sizeof(MSG_S) / sizeof(int);

    os_taskq_post_type(mod_id, msg->msgID, param_num, (int *)msg);
}

/************************************************************************
**@brief: 获取系统CPU占用率
*************************************************************************/
void my_get_os_cpu_usage(void)
{
    int usage[2] = { 0, 0 };
    extern void CacheReport(void);
    CacheReport();
    task_info_output(0);//输出各个线程占用率
    int a = os_cpu_usage(NULL, usage);
    task_info_reset();
    my_log_printf(1, "cpu0:%d%%, cpu1:%d%%", usage[0], usage[1]);//输出总占用率
}

/************************************************************************
**@brief: 检测字符串是不是全是十六进制数据组成(0~9,a,b,c,d,e,f,A,B,C,D,E,F)
**@param[in] str: 输入的字符串
**@return: 返回字符串的长度,0表示错误
*************************************************************************/
uint8 string_check_is_hex_str(const char* str)
{
    uint8 no_count = 0;

    if (str == NULL)
    {
        return 0;
    }

    while (*str)
    {
        if ((*str >= '0' && *str <= '9') ||
            (*str >= 'a' && *str <= 'f') ||
            (*str >= 'A' && *str <= 'F'))
        {
            no_count++;
            str++;
        }
        else
        {
            return 0;
        }
    }
    return no_count;
}

/************************************************************************
**@brief: 将十六进制字符串转换成十六进制数组
**@param[in] dest:      目标数组
**@param[in] dest_size: 目标数组长度
**@param[in] src:       原始字符串
**@return: 返回十六进制数组长度,0表示错误
*************************************************************************/
uint8 hexstr_to_hex(uint8 *dest, uint8 dest_size, const char *src)
{
    uint8 offset = 0;
    uint8 temp_data = 0;
    uint8 hex_data = 0;
    uint8 byte_count = 0;

    if (dest == NULL || src == NULL || dest_size == 0)
    {
        return 0;
    }

    while(*src)
    {
        if (*src >= '0' && *src <= '9')
        {
            temp_data = (uint8)(*src - '0');
        }
        else if (*src >= 'A' && *src <= 'F')
        {
            temp_data = (uint8)(*src - 'A' + 0x0A);
        }
        else if (*src >= 'a' && *src <= 'f')
        {
            temp_data = (uint8)(*src - 'a' + 0x0A);
        }
        else
        {
            return 0;
        }


        if (offset % 2)
        {
            hex_data |= (temp_data & 0x0F);
            dest[byte_count] = hex_data;
            byte_count++;
            
            // 检查缓冲区边界
            if (byte_count >= dest_size) {
                my_log_printf(1, "\r\nsrc string is too large.\r\n");
                break;
            }
        }
        else
        {
            hex_data = ((temp_data << 4) & 0xf0);
        }

        offset++;
        src++;
    }

    return ((offset / 2) + (offset % 2));
}

uint8 hex2ascii(uint8 digit)
{
    uint8 val;

    if (digit <= 9) {
        val = digit - 0x0 + '0';
    } else {
        val = digit - 0xA + 'A';
    }

    return val;
}

/************************************************************************
**@brief: 将十六进制数组转换成十六进制字符串
**@param[in] hex:       十六进制数组
**@param[in] hex_len:   十六进制数组长度
**@param[in] str:       十六进制字符串
**@param[in] str_len:   十六进制字符串长度
*************************************************************************/
void hex2hexstr(uint8 *hex, uint16 hex_len, uint8 *str, uint16 str_len)
{
    uint16 i = 0,j=0;
    uint8 *buf = str;

    if (str_len < 2*hex_len) {
        my_log_printf(1, "str_len is too small.\r\n");
        return;
    }

    while(hex_len--)
    {

        buf[j++] = hex2ascii((hex[i] >> 4) & 0x0f);
        buf[j++] = hex2ascii(hex[i] & 0x0f);
        i++;
    }
    buf[j] = 0;
}

/************************************************************************
**@brief: 检测字符串是不是全是数字组成
**@param[in] flag:  flag & 1 允许字符串中包含'+'或'-'
                    flag & 2 允许字符串中包含'.'
                    flag & 4 数字不允许大于7或小于1
**@param[in] str:   传入的字符串
**@return:          返回有效的字符数
*************************************************************************/
uint8 string_check_is_number(uint8 flag, const char* str)
{
    uint8 no_count = 0;
    while (*str)
    {
        if ((flag & 1) && no_count == 0 && (*str == '+' || *str == '-'))
        {
            no_count++;
            str++;
        }
        else if ((flag & 2) && *str == '.')
        {
            flag &= ~2;
            no_count++;
            str++;
        }
        else if (*str >= '0' && *str <= '9')
        {
            no_count++;
            str++;
        }
        else
        {
            return 0;
        }
    }
    return no_count;
}