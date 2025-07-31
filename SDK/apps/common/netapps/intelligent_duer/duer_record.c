#include "duer_record.h"

#if INTELLIGENT_DUER
#define MY_AUDIO_SAVE_TEST    0

#if MY_AUDIO_SAVE_TEST
static FILE *rec_file = NULL;
#define __file rec_file
#endif

duer_rec_t rec;

static void my_duer_rec_cbuf_init()
{
    if (!rec.buf) {
        rec.buf = my_malloc(DUER_CBUF_SIZE);
    }
    cbuf_init(&rec.cbuf, rec.buf, DUER_CBUF_SIZE);
}

static void my_duer_rec_cbuf_exit(void)
{
    cbuf_clear(&(rec.cbuf));
    my_free(rec.buf);
    rec.buf = NULL;
}

static int my_fwrite(FILE *file, void *buf, u32 size)
{
    int ret = fwrite(buf, size, 1, file);
    return ret;
}

static u16 my_duer_rec_write_data(u8 *voice_buf, u16 voice_len)
{
#if MY_AUDIO_SAVE_TEST
    if (__file) {
        int wlen = my_fwrite(__file, voice_buf, voice_len);
        if (wlen != voice_len) {
            printf("save file err: %d, %d\n", wlen, voice_len);
        }
    }
#endif
    int wlen = cbuf_write(&rec.cbuf, voice_buf, voice_len);
    if (wlen != voice_len) {
        MY_LOG_E("pcm out err: %d, %d\n", wlen, voice_len);
    }
    return 0;
}

int duer_rec_stop(void)
{
    if (!ai_mic_is_busy()) {
        printf("ai_mic_is_null \n\n");
        return true;
    }
    ai_mic_rec_close();
#if MY_AUDIO_SAVE_TEST
    if (__file) {
        fclose(__file);
        __file = NULL;
    }
#endif
    my_duer_rec_cbuf_exit();
    return true;
}


int duer_rec_start(void)
{
    my_duer_rec_cbuf_init();
    if (ai_mic_is_busy()) {
        printf("my_mic_is_busy \n\n");
        return false;
    }
#if MY_AUDIO_SAVE_TEST
    if (__file) {
        fclose(__file);
        __file = NULL;
    }
    __file = fopen("storage/sd0/C/ze.bin", "w+");
    if (!__file) {
        MY_LOG_E("fopen err \n\n");
    }
#endif
    mic_rec_pram_init(DUER_REC_TYPE, 0, my_duer_rec_write_data, 1, 1024);
    ai_mic_rec_start();
    return true;
}
#endif
