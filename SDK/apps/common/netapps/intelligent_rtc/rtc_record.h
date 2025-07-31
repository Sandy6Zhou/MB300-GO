#ifndef _RTC_RECORD_H_
#define _RTC_RECORD_H_



#include "rtc_common.h"
#include "fs.h"
#include "os/os_api.h"

#define MY_REC_SR				16000 // 采样率。和audio_mic_enc_open()函数中的对应
#define MY_REC_LIMIT_MIN_MS				(5000) //录音时间最低限制

#define MY_REC_TYPE           AUDIO_CODING_OPUS
// #define MY_REC_TYPE           AUDIO_CODING_SPEEX

#define MY_CBUF_SIZE	1024*10
#define RTC_REC_AUDIO_LEN		168

extern int ai_mic_is_busy(void);
extern int ai_mic_rec_close(void);
extern int mic_rec_pram_init(/* const char **name,  */u32 enc_type, u8 opus_type, u16(*speech_send)(u8 *buf, u16 len), u16 frame_num, u16 cbuf_size);
extern int ai_mic_rec_start(void);


typedef struct {
    char *buf;
    u8 *read_buf;
    cbuffer_t cbuf;
} rtc_rec_t;


extern int rtc_rec_stop(void);
extern int rtc_rec_start(void);
extern int rtc_get_rec_data(u8 **buf, u32 *len);
extern void rtc_release_rec_data(void *buf);

#endif
