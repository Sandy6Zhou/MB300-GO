#include "apple_dock/iAP.h"
#include "apple_dock/iAP_chip.h"
#include "app_config.h"

#define LOG_TAG_CONST       IAP
#define LOG_TAG             "[iAP_chip]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
#define LOG_CLI_ENABLE
#include "debug.h"

#if TCFG_USB_APPLE_DOCK_EN

static iAP_AUTH_INTERFACE *iap_hdl = NULL;

u16 iAP_chip_auth(u8 cmd, u8 *buf, u16 len)
{
    if ((iap_hdl == NULL) || (!iap_hdl->iap_auth_cmd)) {
        return ((u16) - 1);
    }
    return iap_hdl->iap_auth_cmd(cmd, buf, len);
}

u8 iAP_chip_init(void)
{
    iap_hdl = &iap_auth_iic;//目前只支持iic
    log_info("iAP chip init\n");
    return iap_hdl->iap_auth_init();
}

#endif


