#include "my_platform_list_download.h"

static bool my_net_cbuf_init(void);
static void my_net_cbuf_write(void *buf, int len);
static void my_net_cbuf_exit(void);
net_buf_t net;
static volatile u8 is_last = 0;
/*====================下载器================================*/
void download_state_set_next_url(DownloadState *state)
{
    state->current_url = my_url_get_next();
}

int download_state_prepare_next(DownloadState *state)
{
    if (!state->current_url) {
        return 0;
    }
    state->finished = 0;
    state->total_downloaded_bytes = 0;
    state->total_read_bytes = 0;
    int len = snprintf(state->file_path, sizeof(state->file_path),
                       "storage/sd0/C/song_%d_%ld.mp3",
                       state->file_counter++, (long)os_get_time());
    return !(len < 0 || len >= sizeof(state->file_path));
}

void download_state_cleanup(DownloadState *state)
{
    state->current_url = NULL;
}

void download_state_init(DownloadState *state)
{
    memset(state, 0, sizeof(DownloadState));
    state->file_counter = 1;
    state->current_url = my_url_get_first();
}

/*====================文件管理================================*/
int file_downloader_init(FileDownloader *downloader, DownloadState *state)
{
    memset(downloader, 0, sizeof(FileDownloader));
    downloader->parm.url = state->current_url;
    downloader->parm.cbuf_size = 10 * 1024;
    downloader->parm.timeout_millsec = 10000;
    downloader->parm.seek_threshold = 20 * 1024;
    downloader->parm.save_file = 1;
    downloader->parm.file_dir = state->file_path;
    downloader->parm.dir_len = strlen(state->file_path);

    downloader->net_buf = malloc(NET_BUF_SIZE);
    my_net_cbuf_init();
    if (!downloader->net_buf) {
        MY_LOG_E("Memory allocation failed for URL: %s", state->current_url);
        return 0;
    }

    if (net_download_open(&downloader->net_file, &downloader->parm) != 0) {
        MY_LOG_E("Failed to open download for: %s", state->current_url);
        free(downloader->net_buf);
        return 0;
    }

    return 1;
}

extern void debug_debug(void *arg);
int file_downloader_execute(FileDownloader *downloader,
                            DownloadState *state,
                            DownloadCompleteCallback callback)
{
    int download_status, http_err_status, file_len, read_size, rlen;
    int wait_count = 0;
    const int max_wait = 500;

    while (1) {
        file_len = net_download_get_tmp_data_size(downloader->net_file);
        if (file_len > 0) {
            read_size = (file_len > NET_BUF_SIZE) ? NET_BUF_SIZE : file_len;
            rlen = net_download_read(downloader->net_file, downloader->net_buf, read_size);
            if (rlen > 0) {
                state->total_downloaded_bytes += rlen;
                if (state->total_downloaded_bytes >= 2000) {
                    debug_debug(NULL);
                }
            }
            my_net_cbuf_write(downloader->net_buf, rlen);
        }

        net_download_get_status(downloader->net_file, &download_status, &http_err_status);
        if (download_status == DOWNLOAD_COMPLETE_STATUS) {
            while (cbuf_get_data_len(&net.cbuf) > 512 && wait_count < max_wait) {
                os_time_dly(1);
                wait_count++;
            }
            if (wait_count >= max_wait) {
                MY_LOG_W("Wait playback timeout for %s", state->current_url);
            }
            state->finished = 1;
            if (callback) {
                callback(state);
            }
            return 1;
        }
    }
}

void file_downloader_cleanup(FileDownloader *downloader)
{
    if (downloader->net_file) {
        net_download_close(downloader->net_file);
        downloader->net_file = NULL;
    }
    if (downloader->net_buf) {
        free(downloader->net_buf);
        downloader->net_buf = NULL;
    }
}

/*====================任务驱动================================*/
static TaskStatus g_task_status = {0};


void task_stop_download(void)
{
    g_task_status.run = 0;
    task_kill("dl_task");
}

TaskStatus task_get_status(void)
{
    return g_task_status;
}

static void on_download_complete(DownloadState *state)
{
    printf("Download completed for %s\n", state->current_url);
    printf("Downloaded %d bytes to %s\n",
           state->total_downloaded_bytes, state->file_path);
    if (my_url_is_last()) {
        is_last = 1;
        printf("Last URL downloaded and played\n");
    }
}

static DownloadState dl_state;
void download_music_task(void *p)
{
    FileDownloader downloader;
    download_state_init(&dl_state);
    TaskStatus task_status = task_get_status();

    while (task_status.run && dl_state.current_url) {
        putchar('!');
        if (!download_state_prepare_next(&dl_state)) {
            MY_LOG_E("File path too long for: %s", dl_state.current_url);
            download_state_set_next_url(&dl_state);
            continue;
        }
        if (!file_downloader_init(&downloader, &dl_state)) {
            download_state_set_next_url(&dl_state);
            continue;
        }
        file_downloader_execute(&downloader, &dl_state, on_download_complete);
        file_downloader_cleanup(&downloader);


        download_state_set_next_url(&dl_state);
        task_status = task_get_status();
        os_time_dly(10);
    }
    if (!dl_state.current_url) {
        printf("All downloads completed successfully!\n");
        download_task_exit();
    }
}


void task_start_download(void)
{
    if (g_task_status.run) {
        return;
    }
    is_last = 0;
    g_task_status.run = 1;
    os_task_create(download_music_task, NULL, 31, 10 * 1024, 128, "dl_task");
}

void my_list_download_task()
{
    static u8 initialized = 0;
    if (initialized) {
        MY_LOG_E("Download already initialized");
        return;
    }
    initialized = 1;
    my_print_urls();
    my_url_reset_iterator();
    task_start_download();
}

unsigned long os_get_time()
{
    static unsigned long counter = 0;
    return counter++;
}

static void my_net_download_kill_cb(void)
{
    my_url_reset_iterator();
    my_url_list_destroy();
    download_state_cleanup(&dl_state);
    task_stop_download();
}

static void download_task_exit()
{
    TaskStatus status = task_get_status();
    if (status.run == 0) {
        return;
    }
    os_time_dly(10);
    int msg[3];
    msg[0] = (int)my_net_download_kill_cb;
    msg[1] = ARRAY_SIZE(msg) - 2;
    os_taskq_post_type("app_core", Q_CALLBACK, ARRAY_SIZE(msg), msg);
    os_time_dly(-1);
}





/*=========================cbuf========================*/
#define MY_NET_RX_BUF_SIZE				20*1024

static bool my_net_cbuf_init(void)
{
    if (!net.buf) {
        net.buf = my_malloc(MY_NET_RX_BUF_SIZE);
        if (!net.buf) {
            return false;
        }
    }
    cbuf_init(&net.cbuf, net.buf, MY_NET_RX_BUF_SIZE);
    return true;
}

static void my_net_cbuf_write(void *buf, int len)
{
    int requested_len = len;
    static u32 w_len;
    int wlen = cbuf_write(&net.cbuf, buf, requested_len);
    w_len += wlen;
    if (wlen != requested_len) {
    }
}

static void my_net_cbuf_exit(void)
{
    cbuf_clear(&(net.cbuf));
    my_free(net.buf);
    net.buf = NULL;
}


int my_platform_url_download_status()
{
    return is_last;
}
