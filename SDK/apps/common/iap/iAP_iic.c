#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".iAP_iic.data.bss")
#pragma data_seg(".iAP_iic.data")
#pragma const_seg(".iAP_iic.text.const")
#pragma code_seg(".iAP_iic.text")
#endif
#include "apple_dock/iAP_iic.h"
#include "apple_dock/iAP.h"
#include "apple_dock/iAP_chip.h"
#include "app_config.h"
#include "clock.h"
#include "system/includes.h"

#define LOG_TAG_CONST       IAP
#define LOG_TAG             "[iAP_iic]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
#define LOG_CLI_ENABLE
#include "debug.h"

#if TCFG_USB_APPLE_DOCK_EN

u8 iap_write_addr, iap_read_addr;

#if 0 //0:软件iic,  1:硬件iic
#define _IIC_USE_HW
#endif
#include "iic_api.h"
#define iic_dev                             0

#define IIC_START_BIT		BIT(0)
#define IIC_STOP_BIT		BIT(1)
#define IIC_ACK_BIT			BIT(2)

u8 iic_wbyte(u8 st, u8 dat)
{
    u8 ack_var = 0;
    if (st & IIC_START_BIT) {
        iic_start(iic_dev);
    }
    ack_var = iic_tx_byte(iic_dev, dat);
    if (st & IIC_STOP_BIT) {
        iic_stop(iic_dev);
    }
    /* printf("a=%d,",ack_var); */
    /* return ack_var ? 1 : 0; */
    return ack_var ? 0 : 1;
}

u8 iic_rbyte(u8 st)
{
    u8 iic_dat = 0;
    if (st & IIC_START_BIT) {
        iic_start(iic_dev);
    }
    iic_dat = iic_rx_byte(iic_dev, !(st & IIC_ACK_BIT));
    if (st & IIC_STOP_BIT) {
        iic_stop(iic_dev);
    }
    /* printf("d=0x%x,",iic_dat); */
    return iic_dat;
}

static void iic_start_addr(u8 addr)
{
    u8 cnt = 0xff;
    while (--cnt) {
        if (iic_wbyte(IIC_START_BIT, addr)) {
            delay_nops(200);
            continue;
        } else {
            break;
        }
    }
}

static void iAP_iic_write(u8 iic_addr, u8 iic_dat)
{
    iic_start_addr(iap_write_addr);//Send the I2C write address of the CP
    iic_wbyte(0, iic_addr);
    iic_wbyte(IIC_STOP_BIT, iic_dat);
}

static void iAP_iic_writen(u8 iic_addr, u8 *iic_dat, u8 len)
{
    iic_start_addr(iap_write_addr);//Send the I2C write address of the CP
    iic_wbyte(0, iic_addr);
    for (; len > 1; len--) {
        iic_wbyte(0, *iic_dat++);//写数据
    }
    iic_wbyte(IIC_STOP_BIT, *iic_dat);//写数据
}

static u8 iAP_iic_read(u8 iic_addr)
{
    u8	iic_dat;
    iic_start_addr(iap_write_addr);//Send the I2C write address of the CP
    iic_wbyte(IIC_STOP_BIT, iic_addr);
    iic_start_addr(iap_read_addr);//Send the I2C read address of the CP
    iic_dat = iic_rbyte(IIC_STOP_BIT | IIC_ACK_BIT);
    return iic_dat;
}

static void iAP_iic_readn(u8 iic_addr, u8 *iic_dat, u8 len)
{
    iic_start_addr(iap_write_addr);//Send the I2C write address of the CP
    iic_wbyte(IIC_STOP_BIT, iic_addr);
    iic_start_addr(iap_read_addr);//Send the I2C read address of the CP

    for (; len > 1; len--) {
        *iic_dat++ = iic_rbyte(0);
    }
    *iic_dat = iic_rbyte(IIC_STOP_BIT | IIC_ACK_BIT);
    if ((iap_write_addr == WRITE_ADDR_30) && (iic_addr == ADDR_CHALLENGE_DATA_LENGTH)) {
        *iic_dat = 32;
    }
}

static u16 iAP_iic_X509(u8 *RxBuf, u16 max_len)
{
    u8 certificate_len[2];
    u16 temp_len0, temp_len1;
    log_info("%s", __FUNCTION__);
    iAP_iic_readn(ADDR_ACCESSORY_CERTIFICATE_DATA_LENGTH, certificate_len, 2);
    temp_len0 = ((u16)certificate_len[0] << 8) | certificate_len[1];
    temp_len1 = 0;
    u8 iaddr = ADDR_ACCESSORY_CERTIFICATE_DATA;
    while (temp_len0 != temp_len1) {
        u16 len = temp_len0 - temp_len1;
        if (len > 128) {
            len = 128;
        }
        iAP_iic_readn(iaddr, RxBuf, len);
        iaddr++;
        RxBuf += len;
        temp_len1 += len;
    }
    return temp_len0;
}

#define AC_SIGNATURE_SUCC_GENERATED			0x10
static u16 iAP_iic_crack(u8 *TxBuf, u8 len)
{
    u8 challenge_len[2];
    u8 ac_status = 0;
    u8 retry1;
    u8 retry2 = 20;
    log_info("%s", __FUNCTION__);
    while (1) {
        retry1 = 100;
        iAP_iic_readn(ADDR_CHALLENGE_DATA_LENGTH, challenge_len, sizeof(challenge_len));
        iAP_iic_writen(ADDR_CHALLENGE_DATA, TxBuf, challenge_len[1]);
        iAP_iic_write(ADDR_AUTHENTICATION_CONTROL_STATUS, 0x01);
        while (--retry1) {
            ac_status = iAP_iic_read(ADDR_AUTHENTICATION_CONTROL_STATUS);
            if (AC_SIGNATURE_SUCC_GENERATED == ac_status) {
                return len;
            }
        }
        retry2--;
        if (0 == retry2) {
            return ((u16) - 1);
        }
    }
}

static u16 iAP_iic_signature(u8 *RxBuf, u16 max_len)
{
    u8 signature_len[2];
    log_info("%s", __FUNCTION__);
    iAP_iic_readn(ADDR_SIGNATURE_DATA_LENGTH, signature_len, sizeof(signature_len));
    iAP_iic_readn(ADDR_SIGNATURE_DATA, RxBuf, signature_len[1]);
    return (((u16)signature_len[0] << 8) | signature_len[1]);
}

#define DEVICE_ID_SIZE		4
static const u8 iap_chip_id_20[DEVICE_ID_SIZE] = {0x00, 0x00, 0x02, 0x00};
static const u8 iap_chip_id_30[DEVICE_ID_SIZE] = {0x00, 0x00, 0x03, 0x00};
static u8 iap_iic_init(void)
{
    u8 iap_iic_id[DEVICE_ID_SIZE];
    iic_init(iic_dev, get_iic_config(iic_dev));
    os_time_dly(2);

    //判断是否3.0的芯片
    iap_write_addr = WRITE_ADDR_30;
    iap_read_addr = READ_ADDR_30;
    memset(iap_iic_id, 0, sizeof(iap_iic_id));
    iAP_iic_readn(ADDR_DEVICE_ID, (u8 *)(iap_iic_id), DEVICE_ID_SIZE);
    log_info_hexdump((u8 *)iap_iic_id, DEVICE_ID_SIZE);
    if (memcmp(iap_iic_id, (u8 *)iap_chip_id_30, DEVICE_ID_SIZE) == 0) {
        log_info("mfi id -ok- 3.0");
        return MFI_CHIP_ONLINE;
    }

    //判断是否2.0的芯片
    iap_write_addr = WRITE_ADDR_20;
    iap_read_addr = READ_ADDR_20;
    memset(iap_iic_id, 0, sizeof(iap_iic_id));
    iAP_iic_readn(ADDR_DEVICE_ID, (u8 *)(iap_iic_id), DEVICE_ID_SIZE);
    log_info_hexdump((u8 *)iap_iic_id, DEVICE_ID_SIZE);
    if (memcmp(iap_iic_id, (u8 *)iap_chip_id_20, DEVICE_ID_SIZE) == 0) {
        log_info("mfi id -ok- 2.0");
        return MFI_CHIP_ONLINE;
    }

    log_info("mfi id -err-");
    return MFI_DEVICE_ID_ERROR;
}

static u16 iap_iic_auth(u8 cmd, u8 *buf, u16 len)
{
    switch (cmd) {
    case CMD_READ_X509:
        return iAP_iic_X509(buf, len);
        break;
    case CMD_IAP_CRACK:
        return iAP_iic_crack(buf, len);
        break;
    case CMD_IAP_SIGNATURE:
        return iAP_iic_signature(buf, len);
        break;
    }
    return ((u16) - 1);
}

iAP_AUTH_INTERFACE iap_auth_iic = {
    .iap_auth_init  = iap_iic_init,
    .iap_auth_cmd = iap_iic_auth,
};

#endif /* TCFG_USB_APPLE_DOCK_EN */

