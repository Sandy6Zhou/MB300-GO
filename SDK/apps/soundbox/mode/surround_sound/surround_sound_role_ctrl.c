#include "system/includes.h"
#include "app_action.h"
#include "app_main.h"
#include "default_event_handler.h"
#include "soundbox.h"
#include "tone_player.h"
#include "app_tone.h"
#include "surround_sound.h"


#if TCFG_APP_SURROUND_SOUND_EN && LEA_DUAL_STREAM_MERGE_TRANS_MODE
#if (SURROUND_SOUND_FIX_ROLE_EN == 0)	//不固定角色

enum SURROUND_SOUND_ROLE_ENUM surround_sound_role = SURROUND_SOUND_ROLE_MAX;	//环绕声角色. 默认什么都不是，需外部二次设置

// 设置环绕声的角色
//0：Tx, 1:Rx1_DUAL_L; 2:Rx2_DUAL_R; 3:Rx3_MONO
void set_surround_sound_role(u8 role)
{
    if (role >= 0 && role < SURROUND_SOUND_ROLE_MAX) {
        surround_sound_role = role;
    } else {
        r_printf("err, %s, role:%d\n", __func__, role);
    }
}


u8 get_surround_sound_role(void)
{
    /* u32 rets;//, reti; */
    /* __asm__ volatile("%0 = rets":"=r"(rets)); */
    /* y_printf("Func:%s, role:%d, 0x%x\n", __func__, surround_sound_role,rets );	 */
    return (u8)surround_sound_role;
}


#endif

//根据当前模式，判断是否可以进入广播，返回0代表可以进入广播
u8 surround_sound_broadcast_limit(u8 mode_name)
{

    if ((mode_name != APP_MODE_IIS) && (mode_name != APP_MODE_SURROUND_SOUND)) {
        r_printf("err, 5.1 Surround Sound is enable! Current mode:%d can't support broadcast!\n", mode_name);
        return -1;
    }
#if SURROUND_SOUND_FIX_ROLE_EN
    //角色固定
#if (SURROUND_SOUND_ROLE == 0)
    //发送端(Tx), 只有IIS模式才能打开广播
    if (mode_name != APP_MODE_IIS) {
        r_printf("err, 5.1 Surround Sound is enable! Current mode:%d, Current Role:%d. Please check the mode and role!\n", mode_name, SURROUND_SOUND_ROLE);
        return -1;
    }
#else
    //接收端(Rx1 or Rx2 or Rx3), 只有Surround Sound模式才能打开广播
    if (mode_name != APP_MODE_SURROUND_SOUND) {
        r_printf("err, 5.1 Surround Sound is enable! Current mode:%d, Current Role:%d. Please check the mode and role!\n", mode_name, SURROUND_SOUND_ROLE);
        return -1;
    }
#endif
#else
    //角色不固定
    u8 role = get_surround_sound_role();
    if (role == SURROUND_SOUND_TX) {
        //发送端(Tx), 只有IIS模式才能打开广播
        if (mode_name != APP_MODE_IIS) {
            r_printf("err, 5.1 Surround Sound is enable! Current mode:%d, Current Role:%d. Please check the mode and role!\n", mode_name, role);
            return -1;
        }
    } else if (role > SURROUND_SOUND_TX && role < SURROUND_SOUND_ROLE_MAX) {
        //接收端(Rx1 or Rx2 or Rx3), 只有Surround Sound模式才能打开广播
        if (mode_name != APP_MODE_SURROUND_SOUND) {
            r_printf("err, 5.1 Surround Sound is enable! Current mode:%d, Current Role:%d. Please check the mode and role!\n", mode_name, SURROUND_SOUND_ROLE);
            return -1;
        }
    } else {
        r_printf("err, Surround Sound Role:%d, please check the role!\n", role);
        return -1;
    }
#endif


    return 0;
}




#endif


