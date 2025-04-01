#ifndef _FM_INSIDE_H_
#define _FM_INSIDE_H_

#if(TCFG_FM_INSIDE_ENABLE == ENABLE)

#if DAC2IIS_EN
#define FM_DAC_OUT_SAMPLERATE  44100L
#else
#define FM_DAC_OUT_SAMPLERATE  44100L
#endif

/************************************************************
*真台少：                               假台多：

*增大  SEEK_CNT_MAX(步进1)       减小  SEEK_CNT_MAX(步进1)
*增大  SEEK_CNT_ZERO_MAX(步进1)       减小  SEEK_CNT_ZERO_MAX(步进1)

*
*注意：不要插串口测试搜台数
*************************************************************/

#define FMSCAN_CNR			5
#define SEEK_CNT_MAX		20 	 //SEEK_CNT_MAX  若假台多，步进1减小此值。若想增加模糊台，步进1增大此值。
#define SEEK_CNT_ZERO_MAX	12 	 //SEEK_CNT_ZERO_MAX  若假台多，步进1减小此值。若想增加模糊台，步进1增大此值

u8 fm_inside_init(void *priv);
bool fm_inside_set_fre(void *priv, u16 fre);
bool fm_inside_read_id(void *priv);
u8 fm_inside_powerdown(void *priv);
u8 fm_inside_mute(void *priv, u8 flag);
extern int fm_get_device_en(char *logo);

#endif
#endif // _FM_INSIDE_H_

