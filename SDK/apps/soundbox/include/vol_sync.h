#ifndef __VOL_SYNC_H__
#define __VOL_SYNC_H__

void vol_sys_tab_init(void);
void set_music_device_volume(int volume);
int  phone_get_device_vol(void);
void opid_play_vol_sync_fun(s16 *vol, u8 mode);
void phone_volume_change(s16 *vol);
void vol_sync_mark_remote_update(void);
u8 vol_sync_is_remote_guard_active(void);

#endif
