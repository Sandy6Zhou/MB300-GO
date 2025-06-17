#ifndef __LOUDSPEAKER_H
#define __LOUDSPEAKER_H

#include "system/event.h"

enum {
    IIS_SOURCE,
    MIC_SOURCE,
};

int loudspeaker_start(void);
void loudspeaker_stop(void);
int loudspeaker_volume_pp(void);
void loudspeaker_key_vol_up(void);
void loudspeaker_key_vol_down(void);
struct app_mode *app_enter_loudspeaker_mode(int arg);
int loudspeaker_app_msg_handler(int *msg);
u8 loudspeaker_get_status(void);

u8 get_loudspeaker_source(void);
void set_loudspeaker_source(u8 source);
void loudspeaker_source_switch();


#endif


