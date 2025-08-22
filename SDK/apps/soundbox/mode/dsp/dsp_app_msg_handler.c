#include "key_driver.h"
#include "app_main.h"
#include "init.h"
#include "scene_switch.h"
#include "mic_effect.h"
#include "wireless_trans.h"
#include "le_broadcast.h"
#include "dsp_mode.h"

#if (TCFG_SPI_LCD_ENABLE)
#include "ui/ui_api.h"
#endif

#if TCFG_APP_DSP_EN
int dsp_app_msg_handler(int *msg)
{

    if (false == app_in_mode(APP_MODE_DSP)) {
        return 0;
    }
    switch (msg[0]) {
    case APP_MSG_DSP_OPEN:
        dsp_start();
        break;
    case APP_MSG_DSP_CLOSE:
        dsp_stop();
        break;
    default:
        app_common_key_msg_handler(msg);
        break;
    }

    return 0;
}

#endif

