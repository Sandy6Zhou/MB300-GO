#ifndef SURROUND_SOUND_H
#define SURROUND_SOUND_H


#include "system/event.h"
#include "app_main.h"


// 环绕声广播角色
enum SURROUND_SOUND_ROLE_ENUM {
    SURROUND_SOUND_TX = 0,
    SURROUND_SOUND_RX1_DUAL_L,
    SURROUND_SOUND_RX2_DUAL_R,
    SURROUND_SOUND_RX3_MONO,
    SURROUND_SOUND_ROLE_MAX,
};


struct app_mode *app_enter_surround_sound_mode(int arg);
int surround_sound_app_msg_handler(int *msg);

#if (SURROUND_SOUND_FIX_ROLE_EN == 0)
//以下函数为环绕声不固定角色使用
u8 get_surround_sound_role(void);
void set_surround_sound_role(u8 role);
#endif

u8 surround_sound_broadcast_limit(u8 mode_name);
#endif

