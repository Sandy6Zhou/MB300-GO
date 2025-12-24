/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : my_vm_param.c
 * Brief      : 设备VM参数加载与设置模块
 * Version    : V1.0.0
 * Author     : 周森达(zhousenda@jimiiot.com)
 * Date       : 2025-12-22
************************************************************************************************/

#include "my_common.h"
#include "user_cfg_id.h"
#include "my_findmy_protocol.h"

ConfigParamStruct    gConfigParam = {0};

const AdvValidValue_t gDefaultAdvValidValue =
{
    .GoogleValid = 1,
    .AppleValid = 1,
};

void my_param_load_vm_config(void)
{
    u16 length;
    int ret;

    //--------Load license gg data ---------------------
    length = sizeof(lic_gg_struct);
    ret = syscfg_read(CFG_LICENSE_GG_VALUE, &gConfigParam.lic_gg, length);
    if (ret != length)
    {
        my_log_printf(1, "get vm gg fail");
        memset(&gConfigParam.lic_gg, 0, length);
    }

    //--------Load license ff data ---------------------
    length = sizeof(lic_ff_struct);
    ret = syscfg_read(CFG_LICENSE_FF_VALUE, &gConfigParam.lic_ff, length);
    if (ret != length)
    {
        my_log_printf(1, "get vm ff fail");
        memset(&gConfigParam.lic_ff, 0, length);
    }

    //--------Load adv valid value data ---------------------
    length = sizeof(AdvValidValue_t);
    ret = syscfg_read(CFG_ADV_VALID_VALUE, &gConfigParam.adv_valid_value, length);
    if (ret != length)
    {
        memcpy(&gConfigParam.adv_valid_value, &gDefaultAdvValidValue, length);
        my_log_printf(1, "Adv valid value not found. Use default:GoogleValid(%d),AppleValid(%d)", gConfigParam.adv_valid_value.GoogleValid, gConfigParam.adv_valid_value.AppleValid);
    }

    set_adv_valid_status(GOOGLE_ADV_ID, gConfigParam.adv_valid_value.GoogleValid);
    set_adv_valid_status(APPLE_ADV_ID, gConfigParam.adv_valid_value.AppleValid);
    // my_log_printf(1, "Adv Valid Value=GoogleValid(%d),AppleValid(%d)", ConfigParam.adv_valid_value.GoogleValid, ConfigParam.adv_valid_value.AppleValid);
}

/************************************************************************
**@brief: 设置google数据到flash中
**@param[in] param: 传入的字符串
**@param[in] len:   传入的字符串长度
**@return: ture表示成功，false表示失败
*************************************************************************/
bool my_param_set_gg(char *param, uint8 len)
{
    int ret;
    int lic_stuct_len = sizeof(lic_gg_struct);

    if (len != LICENSE_GG_STR_LEN)
    {
        my_log_printf(1, "my_param_set_gg len error!");
        return false;
    }

    if (string_check_is_hex_str((const char *)param) != LICENSE_GG_STR_LEN)
    {
        my_log_printf(1, "invalid param");
        return false;
    }

    gConfigParam.lic_gg.flag = FLAG_VALID;
    hexstr_to_hex((uint8 *)gConfigParam.lic_gg.hex, sizeof(gConfigParam.lic_gg.hex), param);

    ret = syscfg_write(CFG_LICENSE_GG_VALUE, &gConfigParam.lic_gg, sizeof(lic_gg_struct));
    if (ret != lic_stuct_len)
    {
        my_log_printf(1, "vm set gg Error!!!");
        return false;
    }
    else
    {
        my_log_printf(1, "vm set gg OK!!!");
    }

    return true;
}

const lic_gg_struct *my_param_get_gg(void)
{
    return &gConfigParam.lic_gg;
}

/************************************************************************
**@brief: 设置ios数据到flash中
**@param[in] param: 传入的字符串
**@param[in] len:   传入的字符串长度
**@return: ture表示成功，false表示失败
*************************************************************************/
bool my_param_set_ff(char *param, uint8 len)
{
    int ret;
    int lic_stuct_len = sizeof(lic_ff_struct);

    if (len != LICENSE_FF_STR_LEN)
    {
        my_log_printf(1, "my_param_set_ff len error!");
        return false;
    }

    if (string_check_is_hex_str((const char *)param) != LICENSE_FF_STR_LEN)
    {
        my_log_printf(1, "invalid param");
        return false;
    }

    gConfigParam.lic_ff.flag = FLAG_VALID;
    hexstr_to_hex((uint8 *)gConfigParam.lic_ff.hex, sizeof(gConfigParam.lic_ff.hex), param);

    ret = syscfg_write(CFG_LICENSE_FF_VALUE, &gConfigParam.lic_ff, sizeof(lic_ff_struct));
    if (ret != lic_stuct_len)
    {
        my_log_printf(1, "vm set ff Error!!!");
        return false;
    }
    else
    {
        my_log_printf(1, "vm set ff OK!!!");
    }

    return true;
}

const lic_ff_struct *my_param_get_ff(void)
{
    return &gConfigParam.lic_ff;
}

/************************************************************************
**@brief: 设置哪一路广播数据开启或关闭(google或ios)
**@param[in] cmd:   JGTAG或JATAG
**@param[in] param: ON或OFF
**@return: 0表示成功，负数表示错误
*************************************************************************/
int my_param_set_jgtag_or_jatag(char *cmd, char *param)
{
    int valid_len = sizeof(AdvValidValue_t);
    int ret;

    if (cmd == NULL || param == NULL) {
        my_log_printf(1, "cmd or param is null");
        return -1;
    }

    if (CMD_MATCHED(cmd, "JGTAG"))
    {
        if (CMD_MATCHED(param, "ON"))
        {
            // 设置全局广播对象句柄中的valid值
            set_adv_valid_status(GOOGLE_ADV_ID, 1);

            // 设置存储到flash中
            gConfigParam.adv_valid_value.GoogleValid = 1;
            ret = syscfg_write(CFG_ADV_VALID_VALUE, &gConfigParam.adv_valid_value, sizeof(AdvValidValue_t));
            if (ret != valid_len)
            {
                my_log_printf(1, "vm set jgtag Error!!!");
                return -1;
            }
            else
            {
                my_log_printf(1, "vm set jgtag OK!!!");
            }
        }
        else if (CMD_MATCHED(param, "OFF"))
        {
            // 当苹果广播有效的时候，才允许google广播关闭
            if (gConfigParam.adv_valid_value.AppleValid == 1)
            {
                set_adv_valid_status(GOOGLE_ADV_ID, 0);

                gConfigParam.adv_valid_value.GoogleValid = 0;
                ret = syscfg_write(CFG_ADV_VALID_VALUE, &gConfigParam.adv_valid_value, sizeof(AdvValidValue_t));
                if (ret != valid_len)
                {
                    my_log_printf(1, "vm set jgtag Error!!!");
                    return -1;
                }
                else
                {
                    my_log_printf(1, "vm set jgtag OK!!!");
                }
            }
            else
            {
                return -1;
            }
        }
    }
    else if (CMD_MATCHED(cmd, "JATAG"))
    {
        if (CMD_MATCHED(param, "ON"))
        {
            set_adv_valid_status(APPLE_ADV_ID, 1);

            gConfigParam.adv_valid_value.AppleValid = 1;
            ret = syscfg_write(CFG_ADV_VALID_VALUE, &gConfigParam.adv_valid_value, sizeof(AdvValidValue_t));
            if (ret != valid_len)
            {
                my_log_printf(1, "vm set jatag Error!!!");
                return -1;
            }
            else
            {
                my_log_printf(1, "vm set jatag OK!!!");
            }
        }
        else if (CMD_MATCHED(param, "OFF"))
        {
            if (gConfigParam.adv_valid_value.GoogleValid == 1)
            {
                set_adv_valid_status(APPLE_ADV_ID, 0);

                gConfigParam.adv_valid_value.AppleValid = 0;
                ret = syscfg_write(CFG_ADV_VALID_VALUE, &gConfigParam.adv_valid_value, sizeof(AdvValidValue_t));
                if (ret != valid_len)
                {
                    my_log_printf(1, "vm set jatag Error!!!");
                    return -1;
                }
                else
                {
                    my_log_printf(1, "vm set jatag OK!!!");
                }
            }
            else
            {
                return -1;
            }
        }
    }

    return 0;
}