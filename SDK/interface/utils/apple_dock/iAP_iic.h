#ifndef _IAP_IIC_H_
#define _IAP_IIC_H_

#include "generic/typedef.h"

#define WRITE_ADDR_30  	0x20 //2.0芯片写地址
#define	READ_ADDR_30    0x21 //2.0芯片读地址
#define WRITE_ADDR_20   0x22 //3.0芯片写地址
#define	READ_ADDR_20    0x23 //3.0芯片读地址

#define VALUE_DEVICE_VERSION						0x05 //芯片读回来的DEVICE_VERSION
#define VALUE_FIRMWARE_VERSION						0x01 //芯片读回来的FIRMWARE_VERSION
#define VALUE_AUTHENTICATION_PROTOCOL_MAJOR_VERSION	0x02 //芯片读回来的主版本号
#define VALUE_AUTHENTICATION_PROTOCOL_MINOR_VERSION	0x00 //芯片读回来的从版本号

//Authentication Coprocessor 2.0C register map
#define ADDR_DEVICE_VERSION							0x00 //read only 获取设备版本
#define ADDR_FIRMWARE_VERSION					    0x01 //read only 获取固件版本
#define ADDR_AUTHENTICATION_PROTOCOL_MAJOR_VERSION	0x02 //read only 获取主版本号
#define ADDR_AUTHENTICATION_PROTOCOL_MINOR_VERSION	0x03 //read only 获取从版本号
#define ADDR_DEVICE_ID								0x04 //read only 获取设备ID

//IIC crack
#define ADDR_AUTHENTICATION_CONTROL_STATUS		    0x10 //启动算法或者读取算法结果
#define ADDR_SIGNATURE_DATA_LENGTH 				    0x11 //获取质询响应数据长度
#define ADDR_SIGNATURE_DATA 						0x12 //获取运算结果
#define ADDR_CHALLENGE_DATA_LENGTH				    0x20 //读出验证数据数据长度
#define ADDR_CHALLENGE_DATA 						0x21 //写入32byte运算数据
#define ADDR_ACCESSORY_CERTIFICATE_DATA_LENGTH	    0x30 //获取证书数据长度,附件证书数据长度寄存器保存所存储的X.509证书的长度在CP3.0芯片中,长度在607~609之间变化
#define ADDR_ACCESSORY_CERTIFICATE_DATA			    0x31 //0x31~0x35 读取证书数据
#define ADDR_ACCESSORY_CERTIFICATE_ID               0x40 //获取证书序列号

#endif

