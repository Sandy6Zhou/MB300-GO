#ifndef LE_AUDIO_MIX_MIC_RECORDER_H
#define LE_AUDIO_MIX_MIC_RECORDER_H




//设置是否需要恢复mix mic广播
void set_need_resume_le_audio_mix_mic(u8 en);

//获取是否需要恢复mix mic广播
u8 get_is_need_resume_le_audio_mix_mic(void);

//获取当前是否mix mic广播数据流正在跑
u8 is_le_audio_mix_mic_recorder_running(void);

//打开 mix mic数据流广播叠加
void le_audio_mix_mic_open(void);

//关闭 mix mic数据流广播叠加
void le_audio_mix_mic_close(void);


int get_micEff2LeAudio_switch_status(void);


#endif

