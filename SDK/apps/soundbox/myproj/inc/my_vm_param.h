/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : my_vm_param.h
 * Brief      : 设备VM参数加载与设置模块头文件
 * Version    : V1.0.0
 * Author     : 周森达(zhousenda@jimiiot.com)
 * Date       : 2025-12-22
************************************************************************************************/

#ifndef __MY_VM_PARAM_H__
#define __MY_VM_PARAM_H__

#define LICENSE_GG_STR_LEN                  (24 * 2)
#define LICENSE_FF_STR_LEN                  (29 * 2)

#define FLAG_VALID 0xAA

typedef struct /* 存储的LICENSE GG信息 */
{
    uint8 flag;
    uint8 hex[(LICENSE_GG_STR_LEN / 2) + (LICENSE_GG_STR_LEN % 2)];
}lic_gg_struct;

typedef struct /* 存储的LICENSE FF信息 */
{
    uint8 flag;
    uint8 hex[(LICENSE_FF_STR_LEN / 2) + (LICENSE_FF_STR_LEN % 2)];
}lic_ff_struct;

typedef struct
{
    uint8 GoogleValid;
    uint8 AppleValid;
}AdvValidValue_t;

typedef struct
{
    lic_gg_struct               lic_gg;
    lic_ff_struct               lic_ff;
    AdvValidValue_t             adv_valid_value;
} ConfigParamStruct;

extern ConfigParamStruct    gConfigParam;

void my_param_load_vm_config(void);
bool my_param_set_gg(char *param, uint8 len);
const lic_gg_struct *my_param_get_gg(void);
bool my_param_set_ff(char *param, uint8 len);
const lic_ff_struct *my_param_get_ff(void);
int my_param_set_jgtag_or_jatag(char *cmd, char *param);

#endif