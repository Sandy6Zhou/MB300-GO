#ifndef _DSP_MODE_H_
#define _DSP_MODE_H_

#include "system/event.h"

struct app_mode *app_enter_dsp_mode(int arg);

int dsp_start(void);
void dsp_stop(void);
u8 dsp_get_status(void);

u8 get_dsp_source(void);

void set_dsp_source(u8 source);

int dsp_app_msg_handler(int *msg);
#endif


