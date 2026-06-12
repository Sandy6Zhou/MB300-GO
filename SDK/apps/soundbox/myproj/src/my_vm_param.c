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

#define DEFAULT_ECDH_G_VALUE    0x83A5         // 默认G值

const  GsmImei_t gDefaultImeiValue =
{
    // 默认IMEI为123456789012345
    .flag = 0,
    .hex = {'1','2','3','4','5','6','7','8','9','0','1','2','3','4','5'}
};

const DevSn_t gDefaultSnValue =
{
    .flag = 0,
    .hex = {'0','0','0','0','0','0','0','0','0','0','0','0','0','0','0'}
};

const CfgDcUserParam_t gDefaultCfgDcUserParam =
{
    .flag = FLAG_VALID,
    .transport_delay_hours = DC_TRANSPORT_DELAY_HOURS_DEFAULT,
};

// 加载vm固件存储参数,这接口在shell线程初始化前被调用,所以里面的打印均不能采用自定义打印
void my_param_load_vm_config(void)
{
    u16 length;
    int ret;
    uint8 data_buff[64] = {0};

    //--------Load license gg data ---------------------
    length = sizeof(lic_gg_struct);
    ret = syscfg_read(CFG_LICENSE_GG_VALUE, &gConfigParam.lic_gg, length);
    if (ret != length)
    {
        printf("get vm gg fail\n");
        memset(&gConfigParam.lic_gg, 0, length);
    }

    //--------Load license ff data ---------------------
    length = sizeof(lic_ff_struct);
    ret = syscfg_read(CFG_LICENSE_FF_VALUE, &gConfigParam.lic_ff, length);
    if (ret != length)
    {
        printf("get vm ff fail\n");
        memset(&gConfigParam.lic_ff, 0, length);
    }

    //--------Load adv valid value data ---------------------
    length = sizeof(AdvValidValue_t);
    ret = syscfg_read(CFG_ADV_VALID_VALUE, &gConfigParam.adv_valid_value, length);
    if (ret != length)
    {
        memcpy(&gConfigParam.adv_valid_value, &gDefaultAdvValidValue, length);
        printf("Adv valid value not found. Use default:GoogleValid(%d),AppleValid(%d)\n", gConfigParam.adv_valid_value.GoogleValid, gConfigParam.adv_valid_value.AppleValid);
    }

    set_adv_valid_status(GOOGLE_ADV_ID, gConfigParam.adv_valid_value.GoogleValid);
    set_adv_valid_status(APPLE_ADV_ID, gConfigParam.adv_valid_value.AppleValid);
    // my_log_printf(1, "Adv Valid Value=GoogleValid(%d),AppleValid(%d)", ConfigParam.adv_valid_value.GoogleValid, ConfigParam.adv_valid_value.AppleValid);

    //--------Load ECDH G Value ---------------------
    length = sizeof(gConfigParam.ECDH_GValue);
    ret = syscfg_read(CFG_ECDH_G_VALUE, &gConfigParam.ECDH_GValue, length);
    if (ret != length)
    {
        gConfigParam.ECDH_GValue = DEFAULT_ECDH_G_VALUE;
        printf("ECDH G value not found. Use default:ECDH G value(%04x)\n", gConfigParam.ECDH_GValue);
    }

    //--------Load IMEI Value ---------------------
    length = sizeof(GsmImei_t);
    ret = syscfg_read(CFG_IMEI_VALUE, &gConfigParam.gsm_imei, length);
    if (ret != length)
    {
        memcpy(&gConfigParam.gsm_imei, &gDefaultImeiValue, length);
        memcpy(data_buff, gConfigParam.gsm_imei.hex, sizeof(gConfigParam.gsm_imei.hex));
        printf("imei not found. Use default:imei value(%s)\n", data_buff);
    }

    //--------Load SN Value ---------------------
    length = sizeof(DevSn_t);
    ret = syscfg_read(VM_SN_ADDR, &gConfigParam.dev_sn, length);
    if (ret != length)
    {
        memcpy(&gConfigParam.dev_sn, &gDefaultSnValue, length);
        memcpy(data_buff, gConfigParam.dev_sn.hex, sizeof(gConfigParam.dev_sn.hex));
        data_buff[DEV_SN_LENGTH] = '\0';
        printf("sn not found. Use default:sn value(%s)\n", data_buff);
    }

    //--------Load DC transport delay hours ---------------------
    length = sizeof(CfgDcUserParam_t);
    ret = syscfg_read(CFG_DC_USER_PARAM, &gConfigParam.dc_user_param, length);
    if (ret != length)
    {
        memcpy(&gConfigParam.dc_user_param, &gDefaultCfgDcUserParam, length);
        printf("dc transport delay not found. Use default:%uh\n",
               gConfigParam.dc_user_param.transport_delay_hours);
    }

    /* EMS 事件/告警 outbox */
    my_evt_store_init();
}

bool my_param_set_transport_delay_hours(uint16 hours)
{
    int ret;
    int delay_len = sizeof(CfgDcUserParam_t);

    if (hours != 0 && (hours < DC_TRANSPORT_DELAY_HOURS_MIN || hours > DC_TRANSPORT_DELAY_HOURS_MAX))
    {
        my_log_printf(1, "transport delay set fail, range(0 or %u~%u)",
                      DC_TRANSPORT_DELAY_HOURS_MIN,
                      DC_TRANSPORT_DELAY_HOURS_MAX);
        return false;
    }

    gConfigParam.dc_user_param.flag = FLAG_VALID;
    gConfigParam.dc_user_param.transport_delay_hours = hours;

    ret = syscfg_write(CFG_DC_USER_PARAM,
                       &gConfigParam.dc_user_param, delay_len);
    if (ret != delay_len)
    {
        my_log_printf(1, "vm set transport delay Error!!!");
        return false;
    }

    my_log_printf(1, "vm set transport delay OK!!! hours=%u", hours);
    return true;
}

uint16 my_param_get_transport_delay_hours(void)
{
    return gConfigParam.dc_user_param.transport_delay_hours;
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

int my_param_set_Gvalue(char *param)
{
    uint16 Gvalue;
    int Gvalue_len;
    int ret;

    if (string_check_is_number(0, param))
    {
        Gvalue = atoi(param);

        if (Gvalue < 10000 || Gvalue > 60000)
        {
            my_log_printf(1, "MODIFYGV set fail, range(10000~60000)");
            return -1;
        }
        else
        {
            Gvalue_len = sizeof(gConfigParam.ECDH_GValue);
            gConfigParam.ECDH_GValue = Gvalue;

            ret = syscfg_write(CFG_ECDH_G_VALUE, &gConfigParam.ECDH_GValue, Gvalue_len);
            if (ret != Gvalue_len)
            {
                my_log_printf(1, "vm set Gvalue Error!!!");
                return -1;
            }
            else
            {
                my_log_printf(1, "vm set Gvalue OK!!!");
            }
        }
    }
    else
    {
        return -1;
    }

    return 0;
}

const uint16 my_param_get_Gvalue(void)
{
    return gConfigParam.ECDH_GValue;
}

int my_param_set_imei(char *param, uint8 len)
{
    int ret;
    int GsmImei_struct_len = sizeof(GsmImei_t);

    if (len != GSM_IMEI_LENGTH)
    {
        my_log_printf(1, "my_param_set_imei len error!");
        return -1;
    }

    if (string_check_is_hex_str((const char *)param) != GSM_IMEI_LENGTH)
    {
        my_log_printf(1, "invalid param");
        return -1;
    }

    gConfigParam.gsm_imei.flag = FLAG_VALID;
    memcpy(gConfigParam.gsm_imei.hex, param, len);

    ret = syscfg_write(CFG_IMEI_VALUE, &gConfigParam.gsm_imei, sizeof(GsmImei_t));
    if (ret != GsmImei_struct_len)
    {
        my_log_printf(1, "vm set imei Error!!!");
        return -1;
    }
    else
    {
        my_log_printf(1, "vm set imei OK!!!");
    }

    return 0;
}

const GsmImei_t *my_param_get_imei(void)
{
    return &gConfigParam.gsm_imei;
}

int my_param_set_sn(char *param, uint8 len)
{
    int ret;
    int dev_sn_struct_len = sizeof(DevSn_t);

    if (len != DEV_SN_LENGTH)
    {
        my_log_printf(1, "my_param_set_sn len error!");
        return -1;
    }

    if (string_check_is_hex_str((const char *)param) != DEV_SN_LENGTH)
    {
        my_log_printf(1, "invalid sn param");
        return -1;
    }

    gConfigParam.dev_sn.flag = FLAG_VALID;
    memcpy(gConfigParam.dev_sn.hex, param, len);

    ret = syscfg_write(VM_SN_ADDR, &gConfigParam.dev_sn, sizeof(DevSn_t));
    if (ret != dev_sn_struct_len)
    {
        my_log_printf(1, "vm set sn Error!!!");
        return -1;
    }
    else
    {
        my_log_printf(1, "vm set sn OK!!!");
    }

    return 0;
}

const DevSn_t *my_param_get_sn(void)
{
    return &gConfigParam.dev_sn;
}