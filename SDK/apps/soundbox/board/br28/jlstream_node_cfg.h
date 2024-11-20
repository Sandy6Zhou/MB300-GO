/**
* 注意点：
* 0.此文件变化，在工具端会自动同步修改到工具配置中
* 1.功能块通过【---------xxx------】和 【#endif // xxx 】，是工具识别的关键位置，请勿随意改动
* 2.目前工具暂不支持非文件已有的C语言语法，此文件应使用文件内已有的语法增加业务所需的代码，避免产生不必要的bug
* 3.修改该文件出现工具同步异常或者导出异常时，请先检查文件内语法是否正常
**/

#ifndef JLSTREAM_NODE_CFG_H
#define JLSTREAM_NODE_CFG_H

// ------------提示音宏定义------------
#define TCFG_TONE_EN_ENABLE 1
#define TCFG_TONE_ZH_ENABLE 1
#define TCFG_TONE_AAC_ENABLE 0 // AAC
#define TCFG_TONE_F2A_ENABLE 0 // F2A
#define TCFG_TONE_MP3_ENABLE 1 // MP3
#define TCFG_TONE_MSBC_ENABLE 0 // MSBC
#define TCFG_TONE_MTY_ENABLE 0 // MTY
#define TCFG_TONE_SBC_ENABLE 0 // SBC
#define TCFG_TONE_SIN_ENABLE 1 // SIN
#define TCFG_TONE_WAV_ENABLE 0 // WAV
#define TCFG_TONE_WTG_ENABLE 0 // WTG
#define TCFG_TONE_WTS_ENABLE 0 // WTS
// ------------提示音宏定义------------

// ------------流程图宏定义------------
#define TCFG_AUDIO_BIT_WIDTH 0 // 位宽
#define TCFG_2BAND_MERGE_ENABLE 1 // 2Band Merge
#define TCFG_3BAND_MERGE_ENABLE 1 // 3Band Merge
#define TCFG_3D_PLUS_MODULE_NODE_ENABLE 0 // 3D Plus
#define TCFG_ADC_NODE_ENABLE 1 // ADC
#define TCFG_AUDIO_CVP_DEVELOP_ENABLE 0 // 通话第三方算法
#define TCFG_AUDIO_CVP_DMS_ANS_MODE 0 // 双MIC通话
#define TCFG_AUDIO_CVP_DMS_DNS_MODE 0 // 双MIC+DNS
#define TCFG_AUDIO_CVP_DMS_FLEXIBLE_ANS_MODE 0 // 话务双MIC通话
#define TCFG_AUDIO_CVP_DMS_FLEXIBLE_DNS_MODE 0 // 话务双MIC+DNS
#define TCFG_AUDIO_CVP_SMS_ANS_MODE 1 // 单MIC通话
#define TCFG_AUDIO_CVP_SMS_DNS_MODE 0 // 单MIC+DNS
#define TCFG_AUTO_WAH_NODE_ENABLE 0 // Auto Wah
#define TCFG_AUTODUCK_NODE_ENABLE 0 // AutoDuck、AutoDuck Trigger
#define TCFG_AUTOTUNE_NODE_ENABLE 0 // Autotune
#define TCFG_BASS_TREBLE_NODE_ENABLE 0 // Bass Treble
#define TCFG_CHANNEL_EXPANDER_NODE_ENABLE 1 // Channel Expander
#define TCFG_CHANNEL_MERGE_NODE_ENABLE 1 // Channel Merge
#define TCFG_CHANNEL_SWAP_NODE_ENABLE 0 // 声道交换器
#define TCFG_CHORUS_NODE_ENABLE 0 // Chorus
#define TCFG_CONVERT_NODE_ENABLE 1 // Convert
#define TCFG_CROSSOVER_NODE_ENABLE 1 // 分频器 三段
#define TCFG_DAC_NODE_ENABLE 1 // DAC
#define TCFG_DATA_EXPORT_NODE_ENABLE 0 // data export
#define TCFG_DATA_SATURATION_NODE_ENABLE 0 // Data Saturation
#define TCFG_DISTORTION_NODE_ENABLE 0 // DistortionExp
#define TCFG_DYNAMIC_EQ_EXT_DETECTOR_NODE_ENABLE 0 // Dynamic EQ  Ext Detector
#define TCFG_DYNAMIC_EQ_NODE_ENABLE 0 // Dynamic EQ
#define TCFG_DYNAMIC_EQ_PRO_EXT_DETECTOR_NODE_ENABLE 0 // Dynamic EQ  Pro Ext Detector
#define TCFG_DYNAMIC_EQ_PRO_NODE_ENABLE 0 // Dynamic EQ Pro
#define TCFG_ECHO_NODE_ENABLE 1 // Echo
#define TCFG_EFFECT_DEV0_NODE_ENABLE 0 // EffectDev0
#define TCFG_EFFECT_DEV1_NODE_ENABLE 0 // EffectDev1
#define TCFG_EFFECT_DEV2_NODE_ENABLE 0 // EffectDev2
#define TCFG_EFFECT_DEV3_NODE_ENABLE 0 // EffectDev3
#define TCFG_EFFECT_DEV4_NODE_ENABLE 0 // EffectDev4
#define TCFG_ENERGY_DETECT_NODE_ENABLE 0 // Energy Detect
#define TCFG_EQ_ENABLE 1 // EQ
#define TCFG_ESCO_RX_NODE_ENABLE 1 // ESCO_RX
#define TCFG_ESCO_TX_NODE_ENABLE 1 // ESCO_TX
#define TCFG_FADE_NODE_ENABLE 0 // Fade
#define TCFG_FILE_PACKAGE_NODE_ENABLE 1 // 封装
#define TCFG_FREQUENCY_SHIFT_HOWLING_NODE_ENABLE 1 // Frequency Shift
#define TCFG_GAIN_NODE_ENABLE 0 // Gain
#define TCFG_HARMONIC_EXCITER_NODE_ENABLE 0 // Harmonic Exciter
#define TCFG_HOWLING_GATE_ENABLE 0 // Howling Gate
#define TCFG_IIS_NODE_ENABLE 1 // IIS0_TX
#define TCFG_INDICATOR_NODE_ENABLE 1 // Indicator
#define TCFG_LIMITER_NODE_ENABLE 0 // Limiter
#define TCFG_LLNS_NODE_ENABLE 0 // LLNS
#define TCFG_MIXER_NODE_ENABLE 1 // MIXER
#define TCFG_MULTI_BAND_DRC_NODE_ENABLE 0 // MDRC
#define TCFG_MULTI_BAND_LIMITER_NODE_ENABLE 0 // MB Limiter
#define TCFG_MULTI_CH_IIS_NODE_ENABLE 0 // MULTI CH IIS0 TX
#define TCFG_MULTI_CH_IIS_RX_NODE_ENABLE 0 // MULTI CH IIS0 RX
#define TCFG_MUTE_NODE_ENABLE 0 // mute
#define TCFG_NOISEGATE_NODE_ENABLE 1 // NoiseGate
#define TCFG_NOISEGATE_PRO_NODE_ENABLE 0 // NoiseGate Pro
#define TCFG_NOTCH_HOWLING_NODE_ENABLE 1 // Howling Suppress
#define TCFG_NS_NODE_ENABLE 0 // Noise Suppressor
#define TCFG_PCM_DELAY_NODE_ENABLE 0 // PCM Delay
#define TCFG_PING_PONG_PCM_DELAY_NODE_ENABLE 0 // Pingpong Pcm Delay
#define TCFG_PITCH_SPEED_NODE_ENABLE 0 // Pitch Speed
#define TCFG_PLATE_REVERB_ADVANCE_NODE_ENABLE 0 // Plate Reverb Advance
#define TCFG_PLATE_REVERB_NODE_ENABLE 1 // Plate Reverb
#define TCFG_PLC_NODE_ENABLE 1 // 丢包修复PLC
#define TCFG_REPLACE_NODE_ENABLE 0 // Replace
#define TCFG_REVERB_NODE_ENABLE 0 // Reverb
#define TCFG_SIGNAL_GENERATOR_NODE_ENABLE 0 // SignalGen
#define TCFG_SOFWARE_EQ_NODE_ENABLE 0 // SW EQ
#define TCFG_SOUND_SPLITTER_NODE_ENABLE 1 // 音频分流器
#define TCFG_SPDIF_MASTER_NODE_ENABLE 0 // SPDIF_OUT
#define TCFG_SPEAKER_EQ_NODE_ENABLE 0 // SpeakerEQ
#define TCFG_SPECTRUM_ADVANCE_NODE_ENABLE 0 // Spectrum Advance
#define TCFG_SPECTRUM_NODE_ENABLE 0 // Spectrum
#define TCFG_STEREO_MTAPS_ECHO_NODE_ENABLE 0 // Stereo Mtaps Echo
#define TCFG_STEREO_WIDENER_NODE_ENABLE 0 // Stereo Widener
#define TCFG_STEROMIX_NODE_ENABLE 0 // SteroMix
#define TCFG_SURROUND_NODE_ENABLE 0 // Surround Effect
#define TCFG_SWITCH_NODE_ENABLE 1 // Switch
#define TCFG_SPLIT_GAIN_NODE_ENABLE 0 // Split Gain
#define TCFG_THREE_D_EFFECT_NODE_ENABLE 0 // ThreeD
#define TCFG_UART_NODE_ENABLE 0 // 串口打印
#define TCFG_VBASS_NODE_ENABLE 1 // Virtual Bass
#define TCFG_VIRTUAL_SURROUND_PRO_MODULE_NODE_ENABLE 0 // Virtual Surround 2to5、Virtual Surround Pro
#define TCFG_VIRTUAL_SURROUND_PRO_NODE_ENABLE 0 // upmix 2to5
#define TCFG_VOCAL_REMOVER_NODE_ENABLE 1 // Vocal Remover
#define TCFG_VOCAL_TRACK_SEPARATION_NODE_ENBALE 0 // 声道拆分
#define TCFG_VOCAL_TRACK_SYNTHESIS_NODE_ENABLE 0 // 声道组合
#define TCFG_VOICE_CHANGER_ADV_NODE_ENABLE 0 // Voice Changer Adv
#define TCFG_VOICE_CHANGER_NODE_ENABLE 1 // Voice Changer
#define TCFG_WDRC_ADVANCE_NODE_ENABLE 0 // DRC Advance
#define TCFG_WDRC_DETECTOR_NODE_ENABLE 0 // DRC Detector
#define TCFG_WDRC_NODE_ENABLE 1 // DRC
#define TCFG_WRITE_FILE_NODE_ENABLE 1 // 写文件
#define TCFG_ZERO_ACTIVE_NODE_ENABLE 0 // Zero Active
#define EQ_SECTION_MAX 0xc // EQ_SECTION_MAX
// ------------流程图宏定义------------
#endif


