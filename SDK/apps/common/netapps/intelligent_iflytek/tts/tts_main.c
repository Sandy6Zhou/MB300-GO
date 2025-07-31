#include "tts_main.h"
#include "circular_buf.h"
#include "sparkdesk_main.h"
#include "websocket_define.h"
#include "system/timer.h"
#include "ifly_common.h"

#if TCFG_IFLYTEK_ENABLE

#define TTS_AUDIO_SAVE_TEST     1
#if TTS_AUDIO_SAVE_TEST
static FILE *save_file = NULL;
#endif

#define APP_ID       			TCFG_IFLYTEK_APP_ID

#define RECV_LAST_FRAME  		2

#define IFLY_TTS_PKG_MAX        (1024 * 10)   //经测试，最长一包下发数据的长度为8500bytes左右
#define IFLY_TTS_CBUF_LEN       (IFLY_TTS_PKG_MAX + 4096)
#define TTS_TIMEOUT_TIME  		8  //防止tts在播放过程中收不到消息而卡住，8s没接收消息，就关闭任务


typedef enum {
    IFLY_TTS_STATUS_NULL = 0,
    IFLY_TTS_STATUS_START,	// 启动
    IFLY_TTS_STATUS_SEND,	// 数据已经发给socket
    IFLY_TTS_STATUS_RECV,	// 有接受到数据
    IFLY_TTS_STATUS_RECV_END,// 接受完成
    IFLY_TTS_STATUS_PLAY_END,// 播放完
    IFLY_TTS_STATUS_RECV_ERROR,// 接受错误
    IFLY_TTS_STATUS_EXIT,	// 已经退出
} ifly_tts_status;

struct tts_info_t {
    u8 force_stop; 			// 强制结束
    u8 recv_finish;         // 接受完毕
    u8  tts_to_cnt;			// 超时计数
    u16 tts_timer;			// 超时用的timer ID
    cbuffer_t dec_cbuf;     // 用于tts音频播放的cbuf
    char *dec_out_buf;
    ifly_tts_status status;
    ifly_tts_param *param;
};

static struct tts_info_t tts_info;
static struct ifly_websocket_struct tts_socket;
static void *ifly_tts_usr_task = "app_core";

extern int mbedtls_base64_encode(unsigned char *dst, size_t dlen, size_t *olen, const unsigned char *src, size_t slen);
extern int mbedtls_base64_decode(unsigned char *dst, size_t dlen, size_t *olen, const unsigned char *src, size_t slen);


int a_fwrite(FILE *file, void *buf, u32 size)
{
    int ret = fwrite(buf, size, 1, file);
    return ret;
}

static int test_read(void *priv, void *buf, u32 len, u8 tmp_flag, int tmp_offset)
{
    u32 rlen = 0;

    if (tts_info.status > IFLY_TTS_STATUS_RECV_END) {
        return 0;
    }
    if (tmp_flag) {
        // 格式检查时读数，不保留读取记录
        void *rbuf = cbuf_read_alloc(&tts_info.dec_cbuf, &rlen);
        if (rlen) {
            if (rlen > len) {
                rlen = len;
            }
            memcpy(buf, rbuf, rlen);
        }
    } else {
        // 正常读数
        rlen = cbuf_read(&tts_info.dec_cbuf, buf, len);
        if (rlen != len) {
            if (tts_info.status >= IFLY_TTS_STATUS_RECV_END) {
                rlen = cbuf_get_data_len(&tts_info.dec_cbuf);
                rlen = cbuf_read(&tts_info.dec_cbuf, buf, rlen);
            }
        }
    }
    if (tts_info.status < IFLY_TTS_STATUS_RECV_END) {
        if (rlen == 0) {
            rlen = -1; // 没结束时返回-1挂起解码，不结束解码
        }
    }
    return rlen;
}

static void ifly_net_tts_stop(u32 dat)
{
    int to = dat;
    while (tts_info.dec_out_buf && (tts_info.status < IFLY_TTS_STATUS_PLAY_END)) {
        if (cbuf_get_data_len(&tts_info.dec_cbuf) == 0) {
            break;
        }
        os_time_dly(1);
        if (to < 10) {
            break;
        }
        to -= 10;
    }
    if (tts_info.status < IFLY_TTS_STATUS_PLAY_END) {
        tts_info.status = IFLY_TTS_STATUS_PLAY_END;
    }
#if 0
    ifly_net_tts_dec_close();
#endif
}

static void ifly_net_tts_start(u32 dat)
{
    MY_LOG_I("ifly_net_tts_start");

#if 0
    ifly_net_tts_dec_open(NULL, test_read);
#endif
}

static void ifly_tts_dec_func(void(*func)(u32 dat), int to)
{
    int argv[3];
    argv[0] = (int)func;
    argv[1] = 1;
    argv[2] = to;
    int ret = os_taskq_post_type(ifly_tts_usr_task, Q_CALLBACK, 3, argv);
    if ((ret == OS_ERR_POST_NULL_PTR) || (ret == OS_TASK_NOT_EXIST)) {
        os_taskq_post_type("app_core", Q_CALLBACK, 3, argv);
    }
}

//tts数据模块
char *ifly_tts_format_text_data(void)
{
    char *data_str = NULL;
    int out_len = 0;
    my_cJSON *my_cJSON_test = NULL;
    my_cJSON *my_cJSON_common = NULL;
    my_cJSON *my_cJSON_business = NULL;
    my_cJSON *my_cJSON_data = NULL;

    char *buf = malloc(MAX_SPARKDESK_LEN);
    size_t d_len = MAX_SPARKDESK_LEN;
    mbedtls_base64_encode((unsigned char *)buf, d_len, (size_t *)&out_len, (unsigned char *)tts_info.param->text_res, strlen(tts_info.param->text_res) + 1);
//定义最长回答

    my_cJSON_test = my_cJSON_CreateObject();
    my_cJSON_common = my_cJSON_CreateObject();
    my_cJSON_business = my_cJSON_CreateObject();
    my_cJSON_data = my_cJSON_CreateObject();

    my_cJSON_AddStringToObject(my_cJSON_common, "app_id", APP_ID);
    my_cJSON_AddItemToObject(my_cJSON_test, "common", my_cJSON_common);

    my_cJSON_AddStringToObject(my_cJSON_business, "aue", "lame");
    my_cJSON_AddNumberToObject(my_cJSON_business, "sfl", 1);
    my_cJSON_AddStringToObject(my_cJSON_business, "auf", "audio/L16;rate=16000");
    my_cJSON_AddStringToObject(my_cJSON_business, "vcn", "xiaoyan");
    my_cJSON_AddStringToObject(my_cJSON_business, "tte", "UTF8");
    my_cJSON_AddItemToObject(my_cJSON_test, "business", my_cJSON_business);

    my_cJSON_AddNumberToObject(my_cJSON_data, "status", 2);
    my_cJSON_AddStringToObject(my_cJSON_data, "text", buf);
    my_cJSON_AddItemToObject(my_cJSON_test, "data", my_cJSON_data);

    data_str = my_cJSON_Print(my_cJSON_test);
    free(buf);

    my_cJSON_Delete(my_cJSON_test);

    MY_LOG_I("tts content:%s\n", data_str);

    return data_str;
}

static void tts_recv_timeout_timer(void *priv)
{
    if (tts_info.tts_to_cnt < TTS_TIMEOUT_TIME) {
        tts_info.tts_to_cnt++;
        if (tts_info.tts_to_cnt == TTS_TIMEOUT_TIME) {
            MY_LOG_I("tts timeout!");
            if (tts_info.status < IFLY_TTS_STATUS_RECV_ERROR) {
                tts_info.status = IFLY_TTS_STATUS_RECV_ERROR;
            }
        }
    }
}

//tts解析播放模块
static void ifly_tts_recv_cb(u8 *data, u32 len, u8 type)
{
    char *j_str = (char *)data;
    tts_info.tts_to_cnt = 0;   //收到数据重置定时器

    if (tts_info.force_stop) {
        return;
    }
    if (tts_info.status >= IFLY_TTS_STATUS_RECV_END) {
        return;
    }

    my_cJSON *my_cJSON_root = my_cJSON_Parse(j_str);
    char *audio = NULL;
    char *res_tts = NULL;
    if (my_cJSON_root == NULL) {
        MY_LOG_E("my_cJSON error...\r\n");
        if (tts_info.status <= IFLY_TTS_STATUS_RECV_END) {
            tts_info.status = IFLY_TTS_STATUS_RECV_ERROR;
        }
        return;
    }

    my_cJSON *my_cJSON_data = my_cJSON_GetObjectItem(my_cJSON_root, "data");
    my_cJSON *my_cJSON_status = my_cJSON_GetObjectItem(my_cJSON_data, "status");
    my_cJSON *my_cJSON_audio = my_cJSON_GetObjectItem(my_cJSON_data, "audio");

    audio = my_cJSON_Print(my_cJSON_audio);
    if (!audio) {
        MY_LOG_E("audio is null!!\n");
        tts_info.status = IFLY_TTS_STATUS_RECV_ERROR;
        return;
    }

    str_remove_quote(audio, strlen(audio));

    res_tts = malloc(IFLY_TTS_PKG_MAX);
    if (!res_tts) {
        MY_LOG_E("malloc fail!!\n");
        tts_info.status = IFLY_TTS_STATUS_RECV_ERROR;
        return;
    }
    int olen = 0;
    size_t dlen = IFLY_TTS_PKG_MAX;
    int ret = mbedtls_base64_decode((unsigned char *)res_tts, dlen, (size_t *)&olen, (unsigned char *)audio, strlen(audio));
    if (ret || (olen > dlen)) {
        MY_LOG_E("mbedtls_base64_decode fail!!\n");
        tts_info.status = IFLY_TTS_STATUS_RECV_ERROR;
        return;
    }


#if TTS_AUDIO_SAVE_TEST
    if (save_file) {
        int wlen = a_fwrite(save_file, res_tts, olen);
        if (wlen != olen) {
            MY_LOG_E("save file err: %d, %d\n", wlen, olen);
        }
    }
#endif


    //若cbuf满了，需要先while住，等待播放
    int to = 1500;
    while (!cbuf_write(&tts_info.dec_cbuf, res_tts, olen)) {
        if (tts_info.force_stop) {
            break;
        }
        os_time_dly(1);
        if (to < 10) {
            MY_LOG_I(">>>>>>>>>>>>>>>>>>>>>>>cbuf full");
            break;
        }
        to -= 10;
    }

    if (tts_info.status < IFLY_TTS_STATUS_RECV) {
        tts_info.status = IFLY_TTS_STATUS_RECV;
        MY_LOG_I("tts start play!\n");

        ifly_tts_dec_func(ifly_net_tts_start, 0);
        tts_info.param->event_cb(IFLY_TTS_EVT_PLAY_START, tts_info.param);
    }
    if (my_cJSON_status->valueint == RECV_LAST_FRAME) {
        MY_LOG_I("tts last frame!\n");
        tts_info.status = IFLY_TTS_STATUS_RECV_END;
        tts_info.recv_finish = 1;

        ifly_tts_dec_func(ifly_net_tts_stop, 3000);
        tts_info.param->event_cb(IFLY_TTS_EVT_PLAY_STOP, tts_info.param);
    }

    my_free(audio);
    my_cJSON_Delete(my_cJSON_root);
    free(res_tts);
#if 0
    ifly_net_tts_dec_resume(NULL);
#endif

}

static bool ifly_tts_get_send(u8 **buf, u32 *len)
{
    if (tts_info.force_stop) {
        MY_LOG_I("tts task kill!\n");
        return false;
    }
    if (tts_info.status >= IFLY_TTS_STATUS_PLAY_END) {
        MY_LOG_I("tts task kill!\n");
        return false;
    }
    if (tts_info.status < IFLY_TTS_STATUS_SEND) {
        tts_info.status = IFLY_TTS_STATUS_SEND;
        char *input_src_json = ifly_tts_format_text_data();
        if (input_src_json == NULL) {
            MY_LOG_E("get json err \n");
            return false;
        }
        *buf = (unsigned char *)input_src_json;
        *len = strlen(input_src_json);
        return true;
    }
    os_time_dly(2);
    return true;
}

static int ifly_tts_event_cb(ifly_socket_event_enum evt, void *param)
{
    switch (evt) {
    case IFLY_SOCKET_EVT_SEND_OK:
        if (!tts_info.tts_timer) {
            tts_info.tts_timer = sys_timer_add(NULL, tts_recv_timeout_timer, 1000);
        }
        my_free(param);
        break;
    case IFLY_SOCKET_EVT_SEND_ERROR:
        my_free(param);
        break;
    case IFLY_SOCKET_EVT_INIT_OK:
        break;
    case IFLY_SOCKET_EVT_INIT_ERROR:
    case IFLY_SOCKET_EVT_HANSHACK_ERROR:
    case IFLY_SOCKET_EVT_ACCIDENT_END:
    case IFLY_SOCKET_EVT_END:
#if 0
    case IFLY_SOCKET_EVT_FORCE_END:
        if (ifly_net_tts_dec_check_run()) { // 正常结束会自动关闭，这里为异常或者强制关闭处理
            int to = 10;
            if (evt == IFLY_SOCKET_EVT_ACCIDENT_END) {     //如果是意外退出，先播放完cbuf的内容
                to = 300;
            } else {
                if (tts_info.status < IFLY_TTS_STATUS_RECV_ERROR) {
                    tts_info.status = IFLY_TTS_STATUS_RECV_ERROR;
                }
            }
            ifly_tts_dec_func(ifly_net_tts_stop, to * 10);
            while (ifly_net_tts_dec_check_run()) {
                os_time_dly(1);
                to -= 1;
                if (to <= 0) {
                    break;
                }
                if (tts_info.force_stop) {
                    tts_info.status = IFLY_TTS_STATUS_RECV_ERROR;
                    break;
                }
            }
        }
        if ((evt != IFLY_SOCKET_EVT_END) && (evt != IFLY_SOCKET_EVT_FORCE_END)) {
            if (!tts_info.recv_finish) {
                tts_info.param->event_cb(IFLY_TTS_EVT_PLAY_FAIL_STOP, tts_info.param);
            }
        }
#endif
        break;
    case IFLY_SOCKET_EVT_EXIT:
        tts_info.status = IFLY_TTS_STATUS_EXIT;
        tts_info.param->event_cb(IFLY_TTS_EVT_EXIT, tts_info.param);
        if (tts_socket.auth) {
            free(tts_socket.auth);
            tts_socket.auth = NULL;
        }
        if (tts_info.tts_timer) {
            sys_timer_del(tts_info.tts_timer);
            tts_info.tts_timer = 0;
        }
        if (tts_info.dec_out_buf) {
            free(tts_info.dec_out_buf);
            tts_info.dec_out_buf = NULL;;
        }
        break;
    default:
        break;
    }
    return 0;
}

bool ifly_tts_start(ifly_tts_param *param)
{
    memset(&tts_info, 0, sizeof(tts_info));
    memset(&tts_socket, 0, sizeof(struct ifly_websocket_struct));

    tts_info.param = param;

    tts_info.dec_out_buf = malloc(sizeof(char) * IFLY_TTS_CBUF_LEN);
    cbuf_init(&tts_info.dec_cbuf, tts_info.dec_out_buf, IFLY_TTS_CBUF_LEN);

    tts_socket.auth = ifly_authentication("wss://tts-api.xfyun.cn/v2/tts",
                                          "tts-api.xfyun.cn",
                                          "GET /v2/tts HTTP/1.1");
    if (!tts_socket.auth) {
        free(tts_info.dec_out_buf);
        tts_info.dec_out_buf = NULL;;
        tts_info.param->event_cb(IFLY_TTS_EVT_PLAY_FAIL_STOP, tts_info.param);
        return false;
    }
    tts_socket.task_name = "ifly_tts";
    tts_socket.socket_mode = WEBSOCKET_MODE;
    tts_socket.recv_cb = ifly_tts_recv_cb;
    tts_socket.get_send = ifly_tts_get_send;
    tts_socket.event_cb = ifly_tts_event_cb;

    tts_info.status = IFLY_TTS_STATUS_START;

    //创建链接
    bool ret = ifly_websocket_client_create(&tts_socket);
    if (ret == false) {
        tts_info.status = IFLY_TTS_STATUS_NULL;
        free(tts_socket.auth);
        tts_socket.auth = NULL;
        free(tts_info.dec_out_buf);
        tts_info.dec_out_buf = NULL;;
    }

#if TTS_AUDIO_SAVE_TEST
    if (save_file) {
        fclose(save_file);
        save_file = NULL;
    }
    save_file = fopen("storage/sd0/C/sf.mp3", "w+");
    if (!save_file) {
        MY_LOG_E("fopen err \n\n");
    }
#endif

    return ret;
}


void ifly_tts_stop(u8 force_stop, u32 to_ms)
{
    MY_LOG_I("tts close!\n");
    tts_info.force_stop = force_stop;
    while (tts_socket.auth) { // 结束时auth会自动释放
        os_time_dly(1);
        if (to_ms <= 10) {
            break;
        }
        to_ms -= 10;
    }
    if (to_ms < 1000) {
        to_ms = 1000;
    }
    tts_info.force_stop = 1;
    ifly_websocket_client_release(&tts_socket, to_ms);


#if TTS_AUDIO_SAVE_TEST
    if (save_file) {
        fclose(save_file);
        save_file = NULL;
    }
#endif
}

bool ifly_tts_is_work()
{
    if ((tts_info.status != IFLY_TTS_STATUS_NULL) && (tts_info.status != IFLY_TTS_STATUS_EXIT)) {
        return true;
    }
    return false;
}
#endif

