/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : ble_comu_def.h
 * Brief      : 设备蓝牙交互模块宏定义头文件
 * Version    : V1.0.0
 * Author     : 周森达(zhousenda@jimiiot.com)
 * Date       : 2026-01-14
************************************************************************************************/
#ifndef __BLE_COMU_DEF_H__
#define __BLE_COMU_DEF_H__

#define BLE_DATA_FRAME_LEN_MAX                  16

#define BLE_DATA_PACKET_HEAD                    0X4254          //数据包头
#define BLE_DATA_TYPE_PKEY                      0x1101          //公钥数据
#define BLE_DATA_TYPE_CID                       0x2202          //串号数据
#define BLE_DATA_TYPE_MTU_LEN                   0x2203          //MTU交换数据
#define BLE_DATA_TYPE_HID_VALID                 0x2303          //HID有效期
#define BLE_DATA_TYPE_ALARM                     0x3303          //防盗器指令数据
#define BLE_DATA_TYPE_QUERY                     0x3304          //查询仪表数据及应答
#define BLE_DATA_TYPE_REPORT                    0x3305          //仪表数据上行汇报及应答
#define BLE_DATA_TYPE_CMD                       0x5505          //其它指令数据
#define BLE_DATA_TYPE_FILE_TRANS                0x4605          //文件指令
#define BLE_DATA_TYPE_QEURY_VER                 0x5601          //app下行查询版本号
#define BLE_DATA_TYPE_RSP_VER1                  0x5602          //设备类型
#define BLE_DATA_TYPE_RSP_VER2                  0x5603          //设备版本号

#define BLE_DATA_TYPE_SET_ADV                   0x5606          //设置广播名称
#define BLE_DATA_TYPE_SET_IMEI                  0x5707          //设置IMEI号
#define BLE_DATA_TYPE_AT_CMD                    0x5808          //用户指令
#define BLE_DATA_TYPE_EXPANSION_MODULE          0xFF01          //扩展模块

/* 扩展模块module id */
#define BAT_PERCENT                             0x02            //电量百分比
#define REMAIN_TIME                             0x8004          //剩余可用时间
#define BAT_TEMP                                0x8005          //电池温度
#define OUTPUT_TOTAL_POWER                      0x8006          //输出总功率
#define INPUT_TOTAL_POWER                       0x8007          //输入总功率
#define AC_POWER                                0x8008          //AC功率
#define DC_POWER                                0x8009          //DC功率
#define USB_POWER                               0x800A          //USB功率
#define LED_POWER                               0x800B          //LED功率
#define SW_STATUS                               0x800C          //开关状态
#define FAULT_STATUS                            0x800D          //故障状态

#define BLE_COMU_DEV_CODE                       0xB3
#define BLE_COMU_APP_CODE                       0xC2
#define BLE_COMU_CMD_START                      0x89
#define BLE_COMU_ALARM_START                    0x78

#define BLE_PKEY_RX_CMD                         0x8900C200
#define BLE_PKEY_RSP_DATA1                      0x00
#define BLE_PKEY_RSP_DATA3                      0x00

/* APP下行控制指令    */
#define BLE_APP_CMD_APP_LINK                    0x00
#define BLE_APP_CMD_PAIR_ALLOW                  0x02
#define BLE_APP_CMD_PAIR_ALIOS                  0x03            // IOS PAIR ALLOW
#define BLE_APP_CMD_HID_READ                    0x05
#define BLE_APP_CMD_HID_ONOFF                   0x06
#define BLE_APP_CMD_HID_GEAR                    0x08
#define BLE_APP_CMD_QUERY_SENSOR                0x09
#define BLE_APP_CMD_SETUP_SENSOR                0x0a
#define BLE_APP_CMD_QUERY_BUZZER                0x0b
#define BLE_APP_CMD_SETUP_BUZZER                0x0c
#define BLE_APP_CMD_QUERY_CTRLPWR               0x0d
#define BLE_APP_CMD_SETUP_CTRLPWR               0x0e
#define BLE_APP_CMD_QUERY_ALL                   0x0f
#define BLE_APP_CMD_FIND_CAR                    0x10
#define BLE_APP_CMD_USER_MODE                   0x12
#define BLE_APP_CMD_PAIR_MODE                   0x15
#define BLE_APP_CMD_PAIR_CODE                   0x16

/* 设备上行应答APP控制指令       */
#define BLE_RSP_CMD_APP_LINK                    0x00
#define BLE_RSP_CMD_CID                         0x01
#define BLE_RSP_CMD_PAIR_ALLOW                  0x03
#define BLE_RSP_CMD_PAIR_RESULT                 0x04
#define BLE_RSP_CMD_HID_ONOFF                   0x06
#define BLE_RSP_CMD_HID_GEAR                    0x08
#define BLE_RSP_CMD_SENSOR_PARAM                0x09
#define BLE_RSP_CMD_SENSOR_SETUP                0x0a
#define BLE_RSP_CMD_BUZZER_PARAM                0x0b
#define BLE_RSP_CMD_BUZZER_SETUP                0x0c
#define BLE_RSP_CMD_CTRLPWR_PARAM               0x0d
#define BLE_RSP_CMD_CTRLPWR_SETUP               0x0e
#define BLE_RSP_CMD_QUERY_ALL                   0x0f
#define BLE_RSP_CMD_FIND_CAR                    0x10
#define BLE_RSP_CMD_USER_MODE                   0x12
#define BLE_RSP_CMD_PAIR_MODE                   0x15
#define BLE_RSP_CMD_PAIR_CODE                   0x16

#define BLE_RSP_PARAM_SUCCESS                   0x00
#define BLE_RSP_PARAM_FAIL                      0x01

#endif

