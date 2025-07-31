#include "my_platform_record.h"
#if MY_AUDIO_SAVE_TEST
static FILE *rec_file = NULL;
#define __file rec_file
#else
#define __file NULL
#endif

static my_rec_t rec = {0};

static void my_rec_cbuf_init(void)
{
    if (!rec.buf) {
        rec.buf = my_malloc(MY_CBUF_SIZE);
        ASSERT(rec.buf);
    }
    cbuf_init(&rec.cbuf, rec.buf, MY_CBUF_SIZE);
}

static void my_rec_cbuf_exit(void)
{
    cbuf_clear(&(rec.cbuf));
    my_free(rec.buf);
    rec.buf = NULL;
}

static int my_fwrite(FILE *file, void *buf, u32 size)
{
    return fwrite(buf, size, 1, file);
}

// 音频数据写入回调
static u16 my_rec_write_data(u8 *voice_buf, u16 voice_len)
{
#if MY_AUDIO_SAVE_TEST
    if (__file) {
        size_t wlen = my_fwrite(__file, voice_buf, voice_len);
        if (wlen != voice_len) {
            printf("save file err: %d vs %d\n", (int)wlen, voice_len);
        }
    }
#endif
    int wlen = cbuf_write(&rec.cbuf, voice_buf, voice_len);
    if (wlen != voice_len) {
        MY_LOG_E("pcm out err: %d, %d\n", wlen, voice_len);
    }
    return 0;
}

// 停止录音
int my_rec_stop(void)
{
    if (!ai_mic_is_busy()) {
        printf("mic not recording\n");
        return false;
    }
    ai_mic_rec_close();
#if MY_AUDIO_SAVE_TEST
    if (__file) {
        fclose(__file);
        __file = NULL;
    }
#endif
    my_rec_cbuf_exit();
    return true;
}

// 开始录音
int my_rec_start(void)
{
    if (rec.buf) {
        my_rec_cbuf_exit();
    }
    my_rec_cbuf_init();
    if (ai_mic_is_busy()) {
        printf("mic is busy\n");
        goto err_exit;
    }
#if MY_AUDIO_SAVE_TEST
    if (__file) {
        fclose(__file);
    }
    __file = fopen(MY_AUDIO_SAVE_PATH, "wb");
    if (!__file) {
        MY_LOG_E("fopen error");
        goto err_exit;
    }
#endif
    if (mic_rec_pram_init(MY_REC_TYPE, 0, my_rec_write_data, 1, 1024)) {
        MY_LOG_E("rec param init fail");
        goto err_exit;
    }
    if (ai_mic_rec_start()) {
        MY_LOG_E("rec start fail");
        goto err_exit;
    }
    return true;
err_exit:
    my_rec_stop();
    return false;
}
