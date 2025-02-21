#ifndef _IAP_CHIP_H_
#define _IAP_CHIP_H_

#include "generic/typedef.h"

//iAP cmd
#define CMD_READ_X509                       0x01 //获取X509证书
#define CMD_IAP_CRACK                       0x02 //解密运算
#define CMD_IAP_SIGNATURE                   0x03 //获取签名(质询响应数据)

//MFI CHIP ONLINE STATUS
enum {
    MFI_CHIP_OFFLINE = 0,
    MFI_CHIP_ONLINE,
    MFI_DEVICE_ID_ERROR,
};

typedef struct {
    u8(*iap_auth_init)(void);
    u16(*iap_auth_cmd)(u8 cmd, u8 *buffer, u16 len);
} iAP_AUTH_INTERFACE;

extern u16 iAP_chip_auth(u8 cmd, u8 *buf, u16 len);
extern u8 iAP_chip_init(void);
extern iAP_AUTH_INTERFACE iap_auth_iic;

#endif

