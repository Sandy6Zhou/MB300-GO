#include "sparkdesk_main.h"
#include "websocket_define.h"
#include "ifly_common.h"

#if TCFG_IFLYTEK_ENABLE

#define APP_ID       			TCFG_IFLYTEK_APP_ID

#define MAX_SPARKDESK_TOKENS   	((MAX_SPARKDESK_LEN - 100) / 6) //1个tokens相当于1.5个汉字，1个汉字最大4个byte，即1个tokens约6个byte
#define RECV_LAST_FRAME   		2

typedef enum {
    IFLY_AI_STATUS_NULL = 0,
    IFLY_AI_STATUS_START,	// 启动
    IFLY_AI_STATUS_SEND,	// 数据已经发给socket
    IFLY_AI_STATUS_RECV,	// 有接受到数据
    IFLY_AI_STATUS_RECV_END,// 接受完成
    IFLY_AI_STATUS_RECV_ERROR,// 接受错误
    IFLY_AI_STATUS_EXIT,	// 已经退出
} ifly_ai_status;

struct sparkdesk_info_t {
    u8 force_stop;
    u8 recv_finish;
    ifly_ai_status status;
    ifly_ai_param *param;
};

static struct sparkdesk_info_t sparkdesk_info;
static struct ifly_websocket_struct sparkdesk_socket;

//json解析函数_ai
static void ifly_sparkdesk_recv_cb(u8 *j_str, u32 len, u8 type)
{
    if (sparkdesk_info.force_stop) {
        return;
    }
    if (sparkdesk_info.status >= IFLY_AI_STATUS_RECV_END) {
        return;
    }
    my_cJSON *my_cJSON_root = my_cJSON_Parse((char *)j_str);
    if (my_cJSON_root == NULL) {
        MY_LOG_E("my_cJSON error...\r\n");
        if (sparkdesk_info.status <= IFLY_AI_STATUS_RECV_END) {
            sparkdesk_info.status = IFLY_AI_STATUS_RECV_ERROR;
        }
        return;
    }
    sparkdesk_info.status = IFLY_AI_STATUS_RECV;

    my_cJSON *my_cJSON_header = my_cJSON_GetObjectItem(my_cJSON_root, "header");
    my_cJSON *my_cJSON_status = my_cJSON_GetObjectItem(my_cJSON_header, "status");
    my_cJSON *my_cJSON_payload = my_cJSON_GetObjectItem(my_cJSON_root, "payload");
    my_cJSON *my_cJSON_choices = my_cJSON_GetObjectItem(my_cJSON_payload, "choices");
    my_cJSON *my_cJSON_text = my_cJSON_GetObjectItem(my_cJSON_choices, "text");

    int arr_size = my_cJSON_GetArraySize(my_cJSON_text);
    my_cJSON *arr_item = my_cJSON_text->child;
    u32 ai_res_len = strlen(sparkdesk_info.param->ai_res);
    for (int i = 0; i < arr_size; i++) {
        my_cJSON *my_cJSON_content = my_cJSON_GetObjectItem(arr_item, "content");
        char *my_cJSON_str = my_cJSON_Print(my_cJSON_content);
        u32 json_len = strlen(my_cJSON_str);
        if ((ai_res_len + json_len + 1) > sparkdesk_info.param->ai_res_len) {
            MY_LOG_E("len error\n");
        } else {
            strcpy(&sparkdesk_info.param->ai_res[ai_res_len], my_cJSON_str);
            ai_res_len += json_len;
        }
        arr_item = arr_item->next;
        my_free(my_cJSON_str);
    }

    int res_len = strlen(sparkdesk_info.param->ai_res);

    str_remove_quote(sparkdesk_info.param->ai_res, res_len);

    MY_LOG_I("final res:%s\n", sparkdesk_info.param->ai_res);

    if (my_cJSON_status->valueint == RECV_LAST_FRAME) {
        sparkdesk_info.status = IFLY_AI_STATUS_RECV_END;
        sparkdesk_info.recv_finish = 1;
        sparkdesk_info.param->event_cb(IFLY_AI_EVT_RECV_OK, sparkdesk_info.param);
    }

    my_cJSON_Delete(my_cJSON_root);
}


//ai数据模块
static char *ifly_sparkdesk_format_audio_data(void)
{
    char *data_str = NULL;
    my_cJSON *my_cJSON_test = NULL;
    my_cJSON *my_cJSON_header = NULL;
    my_cJSON *my_cJSON_parameter = NULL;
    my_cJSON *my_cJSON_payload = NULL;
    my_cJSON *my_cJSON_chat = NULL;
    my_cJSON *my_cJSON_message = NULL;
    my_cJSON *my_cJSON_text = NULL;
    my_cJSON *my_cJSON_texts = NULL;

    my_cJSON_test = my_cJSON_CreateObject();
    my_cJSON_header = my_cJSON_CreateObject();
    my_cJSON_parameter = my_cJSON_CreateObject();
    my_cJSON_payload = my_cJSON_CreateObject();
    my_cJSON_chat = my_cJSON_CreateObject();
    my_cJSON_message = my_cJSON_CreateObject();
    my_cJSON_text = my_cJSON_CreateObject();

    my_cJSON_AddStringToObject(my_cJSON_header, "app_id", APP_ID);
    my_cJSON_AddItemToObject(my_cJSON_test, "header", my_cJSON_header);

    my_cJSON_AddStringToObject(my_cJSON_chat, "domain", "generalv3");
    my_cJSON_AddNumberToObject(my_cJSON_chat, "max_tokens", MAX_SPARKDESK_TOKENS);
    my_cJSON_AddItemToObject(my_cJSON_parameter, "chat", my_cJSON_chat);
    my_cJSON_AddItemToObject(my_cJSON_test, "parameter", my_cJSON_parameter);

    my_cJSON_texts = my_cJSON_AddArrayToObject(my_cJSON_message, "text");
    my_cJSON_AddStringToObject(my_cJSON_text, "role", "user");
    my_cJSON_AddStringToObject(my_cJSON_text, "content", sparkdesk_info.param->content);
    my_cJSON_AddItemToArray(my_cJSON_texts, my_cJSON_text);
    my_cJSON_AddItemToObject(my_cJSON_payload, "message", my_cJSON_message);
    my_cJSON_AddItemToObject(my_cJSON_test, "payload", my_cJSON_payload);

    data_str = my_cJSON_Print(my_cJSON_test);

    my_cJSON_Delete(my_cJSON_test);

    return data_str;
}

static bool ifly_sparkdesk_get_send(u8 **buf, u32 *len)
{
    if (sparkdesk_info.force_stop) {
        MY_LOG_I("ai task kill!\n");
        return false;
    }
    if (sparkdesk_info.status >= IFLY_AI_STATUS_RECV_END) {
        MY_LOG_I("ai task kill!\n");
        return false;
    }
    if (sparkdesk_info.status < IFLY_AI_STATUS_SEND) {
        sparkdesk_info.status = IFLY_AI_STATUS_SEND;
        char *input_src_json = ifly_sparkdesk_format_audio_data();
        if (input_src_json == NULL) {
            MY_LOG_E("get json err \n");
            return false;
        }
        *buf = (u8 *)input_src_json;
        *len = strlen(input_src_json);
        return true;
    }
    os_time_dly(2);
    return true;
}

static int ifly_sparkdesk_event_cb(ifly_socket_event_enum evt, void *param)
{
    switch (evt) {
    case IFLY_SOCKET_EVT_SEND_OK:
        my_free(param);
        os_time_dly(5);
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
    case IFLY_SOCKET_EVT_FORCE_END:
        if ((evt != IFLY_SOCKET_EVT_END) && (evt != IFLY_SOCKET_EVT_FORCE_END)) {
            if (!sparkdesk_info.recv_finish) {
                sparkdesk_info.param->event_cb(IFLY_AI_EVT_NETWORK_FAIL, sparkdesk_info.param);
            }
        }
        break;
    case IFLY_SOCKET_EVT_EXIT:
        sparkdesk_info.status = IFLY_AI_STATUS_EXIT;
        sparkdesk_info.param->event_cb(IFLY_AI_EVT_EXIT, sparkdesk_info.param);
        if (sparkdesk_socket.auth) {
            my_free(sparkdesk_socket.auth);
            sparkdesk_socket.auth = NULL;
        }
        break;
    default:
        break;
    }
    return 0;
}

bool ifly_sparkdesk_start(ifly_ai_param *param)
{
    memset(&sparkdesk_info, 0, sizeof(struct sparkdesk_info_t));
    memset(&sparkdesk_socket, 0, sizeof(struct ifly_websocket_struct));

    sparkdesk_info.param = param;
    sparkdesk_socket.auth = ifly_authentication("ws://spark-api.xf-yun.com/v3.1/chat",
                            "spark-api.xf-yun.com",
                            "GET /v3.1/chat HTTP/1.1");
    if (!sparkdesk_socket.auth) {
        sparkdesk_info.param->event_cb(IFLY_AI_EVT_NETWORK_FAIL, sparkdesk_info.param);
        return false;
    }

    sparkdesk_socket.task_name = "ifly_ai";
    sparkdesk_socket.socket_mode = WEBSOCKET_MODE;
    sparkdesk_socket.recv_cb = ifly_sparkdesk_recv_cb;
    sparkdesk_socket.get_send = ifly_sparkdesk_get_send;
    sparkdesk_socket.event_cb = ifly_sparkdesk_event_cb;

    sparkdesk_info.status = IFLY_AI_STATUS_START;

    //创建链接
    bool ret = ifly_websocket_client_create(&sparkdesk_socket);
    if (ret == false) {
        sparkdesk_info.status = IFLY_AI_STATUS_NULL;
        my_free(sparkdesk_socket.auth);
        sparkdesk_socket.auth = NULL;
    }

    return ret;
}

void ifly_sparkdesk_stop(u8 force_stop, u32 to_ms)
{
    MY_LOG_I("ai stop!\n");
    sparkdesk_info.force_stop = force_stop;
    while (sparkdesk_socket.auth) { // 结束时auth会自动释放
        os_time_dly(1);
        if (to_ms <= 10) {
            break;
        }
        to_ms -= 10;
    }
    if (to_ms < 1000) {
        to_ms = 1000;
    }
    sparkdesk_info.force_stop = 1;
    ifly_websocket_client_release(&sparkdesk_socket, to_ms);
}

bool ifly_sparkdesk_is_work()
{
    if ((sparkdesk_info.status != IFLY_AI_STATUS_NULL) && (sparkdesk_info.status != IFLY_AI_STATUS_EXIT)) {
        return true;
    }
    return false;
}

#endif

