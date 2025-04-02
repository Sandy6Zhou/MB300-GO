#ifndef __MIC_H
#define __MIC_H


#include "system/event.h"

int mic_start(void);
void mic_stop(void);
int mic_volume_pp(void);
void mic_key_vol_up(void);
void mic_key_vol_down(void);
u8 mic_get_status(void);
struct app_mode *app_enter_mic_mode(int arg);
int mic_app_msg_handler(int *msg);

//下面函数为了解决问题：如果固定为接收端，则会存在的问题：打开广播下从其它模式切进mic模式，关闭广播mic不会自动打开的问题
extern void mic_set_local_open_flag(u8 en);

#endif

