#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".vol_sync.data.bss")
#pragma data_seg(".vol_sync.data")
#pragma const_seg(".vol_sync.text.const")
#pragma code_seg(".vol_sync.text")
#endif
#include "typedef.h"
#include "app_main.h"
#include "app_config.h"
#include "vol_sync.h"
#include "audio_config.h"
#include "app_tone.h"
#include "volume_node.h"
#include "bt_tws.h"
#include "generic/jiffies.h"
#include "system/timer.h"

u8 vol_sys_tab[17] =  {0, 2, 3, 4, 6, 8, 10, 11, 12, 14, 16, 18, 19, 20, 22, 23, 25};
const u8 vol_sync_tab[17] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120, 127};

static s16 max_vol = 100;
/* 手机下发音量后开启短暂 guard 窗口，
 * 窗口内抑制板端反向回传，避免 phone->box->phone 回环。 */
static u32 s_remote_guard_until_ms = 0;
/* 手机连续滑动时不断重启定时器，窗口结束只应用最后一次值。 */
static u16 s_remote_apply_tid = 0;
/* 合并窗口内缓存的最后手机音量。 */
static u8 s_remote_pending_phone_vol = 0xFF;

static void remote_apply_pending_cb(void *priv)
{
    u8 phone_vol = s_remote_pending_phone_vol;
    s16 music_volume = 0;

    (void)priv;
    s_remote_apply_tid = 0;
    if (phone_vol > 127)
    {
        /* 非法值直接丢弃，避免异常设备上报污染本地状态。 */
        return;
    }

    /* 播放提示音/来电铃声/通话编解码忙时，不立即改 DAC，避免与高优先级音频路径抢占。 */
    if (tone_player_runing() || ring_player_runing() || bt_get_esco_coder_busy_flag())
    {
        // log_i("[VOLSYNC][REMOTE_FLUSH_SKIP_BUSY] phone=%d\n", phone_vol);
        app_var.music_volume = ((phone_vol + 1) * max_vol) / 127;
        return;
    }

    music_volume = ((phone_vol + 1) * max_vol) / 127;
    phone_volume_change(&music_volume);

    /* 去重：映射后的本地档位未变化则不重复设置，降低无效处理开销。 */
    if (music_volume == app_var.music_volume)
    {
        return;
    }

    app_var.opid_play_vol_sync = vol_sync_tab[(phone_vol + 1) / 8];
    // printf("[VOLSYNC][REMOTE_FLUSH_APPLY] phone=%d opid=%d dac=%d", phone_vol, app_var.opid_play_vol_sync, music_volume);
    app_audio_set_volume(APP_AUDIO_STATE_MUSIC, music_volume, 1);
    app_audio_set_volume_def_state(0);
}

static void remote_apply_schedule(u8 phone_vol)
{
    /* 每次新输入覆盖 pending，保证停止位置最终生效。 */
    s_remote_pending_phone_vol = phone_vol;
    if (s_remote_apply_tid)
    {
        sys_timeout_del(s_remote_apply_tid);
    }

    /* 120ms 合并窗口：窗口结束后统一落地一次。 */
    s_remote_apply_tid = sys_timeout_add(NULL, remote_apply_pending_cb, 120);
}

void vol_sync_mark_remote_update(void)
{
    /* 收到手机下行音量后刷新 guard 窗口。 */
    s_remote_guard_until_ms = jiffies_msec() + 400;
}

u8 vol_sync_is_remote_guard_active(void)
{
    u32 now = jiffies_msec();
    /* 当前时间仍早于 guard 截止时间则返回 active，
     * 发送侧据此临时抑制反向回传，避免形成同步回环。 */
    return (now < s_remote_guard_until_ms) ? 1 : 0;
}

void vol_sys_tab_init(void)
{
#if TCFG_BT_VOL_SYNC_ENABLE

#if 1
    u8 i = 0;
    max_vol = app_audio_volume_max_query(AppVol_BT_MUSIC);
    for (i = 0; i < 17; i++) {
        vol_sys_tab[i] = i * max_vol / 16;
    }
#else
    u8 i = 0;
    max_vol = app_audio_volume_max_query(AppVol_BT_MUSIC);
    vol_sys_tab[0] = 0;

    //最大音量<16，补最大值
    if (max_vol <= 16) {
        for (i = 1; i <= max_vol; i++) {
            vol_sys_tab[i] = i;
        }
        for (; i < 17; i++) {
            vol_sys_tab[i] = max_vol;
        }
    } else {
        u8 j = max_vol - 16;
        u8 k = 1;
        for (i = 1; i <= max_vol; i++) {
            /* g_printf("i=%d j=%d  k=%d",i,j,k); */
            if (i % 2) {
            } else {
                //忽略多余的级数
                if (j > 0) {
                    j--;
                    continue;
                }
            }

            if (k < 17) {
                vol_sys_tab[k] = i;
            }
            k++;
        }

        vol_sys_tab[16] = max_vol;
    }

#endif

#if 0
    for (i = 0; i < 17; i++) {
        g_printf("[%d]:%d ", i, vol_sys_tab[i]);
    }
#endif

#endif
}

//注册给库的回调函数，用户手机设置设备音量
void set_music_device_volume(int volume)
{
#if TCFG_BT_VOL_SYNC_ENABLE
    s16 music_volume;

    //音量同步最大是127，请计数比例
    if (volume > 127) {
        /*log_info("vol %d invalid\n",  volume);*/
#if TCFG_VOL_RESET_WHEN_NO_SUPPORT_VOL_SYNC
        /*不支持音量同步的设备，默认设置到最大值，可以根据实际需要进行修改。
         注意如果不是最大值，设备又没有按键可以调音量到最大，则输出也就达不到最大*/
        music_volume = max_vol;
        log_i("unsupport vol_sync,reset vol:%d\n", music_volume);
        app_audio_set_volume(APP_AUDIO_STATE_MUSIC, music_volume, 1);
#endif
        return;
    }

    /* 输入层只负责打标+排队，实际落地在 flush 回调里执行。
     * 这样可把快速滑动期间的大量中间值合并成末值。 */
    vol_sync_mark_remote_update();
    remote_apply_schedule((u8)volume);
#endif
}

//注册给库的回调函数，用于手机获取当前设备音量
int phone_get_device_vol(void)
{
    //音量同步最大是127，请计数比例
#if 0
    return (app_var.sys_vol_l * max_vol / 127) ;
#else
    return app_var.opid_play_vol_sync;
#endif
}


void opid_play_vol_sync_fun(s16 *vol, u8 mode)
{
#if TCFG_BT_VOL_SYNC_ENABLE
    vol_sys_tab[16] =  max_vol;

    if (*vol == 0) {
        if (mode) {
            *vol = vol_sys_tab[1];
            app_var.opid_play_vol_sync = vol_sync_tab[1];
        } else {
            *vol = vol_sys_tab[0];
            app_var.opid_play_vol_sync = vol_sync_tab[0];
        }
    } else if (*vol >= max_vol) {
        if (mode) {
            *vol = vol_sys_tab[16];
            app_var.opid_play_vol_sync = vol_sync_tab[16];
        } else {
            *vol = vol_sys_tab[15];
            app_var.opid_play_vol_sync = vol_sync_tab[15];
        }
    } else {
        for (u8 i = 0; i < sizeof(vol_sys_tab); i++) {
            if (*vol == vol_sys_tab[i]) {
                if (mode) {
                    app_var.opid_play_vol_sync = vol_sync_tab[i + 1];
                    *vol = vol_sys_tab[i + 1];
                } else {
                    app_var.opid_play_vol_sync = vol_sync_tab[i - 1];
                    *vol = vol_sys_tab[i - 1];
                }
                break;
            } else if (*vol < vol_sys_tab[i]) {
                if (mode) {
                    if (16 <= i) {
                        *vol = vol_sys_tab[16];
                    } else {
                        *vol = vol_sys_tab[i + 1];
                    }
                    app_var.opid_play_vol_sync = vol_sync_tab[i];
                } else {
                    *vol = vol_sys_tab[i - 1];
                    app_var.opid_play_vol_sync = vol_sync_tab[i - 1];
                }
                break;
            }
        }
    }
#endif
}

//给手机设置设备音量使用，和音量增减一样使用查表赋值。
void phone_volume_change(s16 *vol)
{
#if TCFG_BT_VOL_SYNC_ENABLE
    vol_sys_tab[16] =  max_vol;

    if (*vol == 0) {
        *vol = vol_sys_tab[0];
    } else if (*vol >= max_vol) {
        *vol = vol_sys_tab[16];
    } else {
        for (u8 i = 0; i < sizeof(vol_sys_tab); i++) {
            if (*vol == vol_sys_tab[i]) {
                *vol = vol_sys_tab[i];
                break;
            } else if (*vol < vol_sys_tab[i]) {
                if (*vol < vol_sys_tab[i] - 3) {
                    *vol = vol_sys_tab[i - 1];
                } else {
                    *vol = vol_sys_tab[i];
                }
                break;
            }
        }
    }
#endif
}


