#include "websocket_api.h"
#include "rtc_common.h"
#include "circular_buf.h"
#include "fs.h"
#include "rtc_record.h"
/* static FILE *fp; */
#if INTELLIGENT_RTC
#define OBJ_URL 	"ws://ai.agent.kaywang.cn:8988/api/v1/realtime?a=apprdgqkjjn9cjc&ac=opus"
/* #define OBJ_URL 	"ws://ai.agent.kaywang.cn:8988/api/v1/realtime?a=apprdgqkjjn9cjc" */
static void task_kill_callback(char *buf);

static void websockets_callback(u8 *buf, u32 len, u8 type)
{
    MY_LOG_I("wbs recv msg : %s\n", buf);
    /* printf(">>>zwz info: %s %d %s\n", __FUNCTION__, __LINE__, __FILE__); */
    /* put_buf(buf,len); */
    /* printf(">>>zwz info: %s %d %s\n", __FUNCTION__, __LINE__, __FILE__); */
    if (strstr((char *)buf, "[COMING]") != NULL) {
        printf("is comming!!!\n"); // 检测到关键字时打印A
        /*
        if(fp)
        {
        	fwrite(buf, 1, len, fp);
        }
        */
    }
}
extern rtc_rec_t rc;
extern int task_kill(const char *name);
/*******************************************************************************
*   Websocket Client api
*******************************************************************************/
static void websockets_client_reg(struct websocket_struct *websockets_info, char mode)
{
    memset(websockets_info, 0, sizeof(struct websocket_struct));
    websockets_info->_init           = websockets_client_socket_init;
    websockets_info->_exit           = websockets_client_socket_exit;
    websockets_info->_handshack      = webcockets_client_socket_handshack;
    websockets_info->_send           = websockets_client_socket_send;
    websockets_info->_recv_thread    = websockets_client_socket_recv_thread;
    websockets_info->_heart_thread   = websockets_client_socket_heart_thread;
    websockets_info->_recv_cb        = websockets_callback;
    websockets_info->_recv           = NULL;
    websockets_info->websocket_mode  = mode;
}

static int websockets_client_init(struct websocket_struct *websockets_info, u8 *url, const char *origin_str, const char *user_agent_str)
{
    websockets_info->ip_or_url = url;
    websockets_info->origin_str = origin_str;
    websockets_info->user_agent_str = user_agent_str;
    websockets_info->recv_time_out = 1000;

    //应用层和库的版本检测，结构体不一样则返回出错
    int err = websockets_struct_check(sizeof(struct websocket_struct));
    if (err == FALSE) {
        return err;
    }
    return websockets_info->_init(websockets_info);
}

static int websockets_client_handshack(struct websocket_struct *websockets_info)
{
    MY_LOG_I("myurl %s \n", websockets_info->ip_or_url);
    return websockets_info->_handshack(websockets_info);
}

static int websockets_client_send(struct websocket_struct *websockets_info, u8 *buf, int len, char type)
{
    //SSL加密时一次发送数据不能超过16K，用户需要自己分包
    return websockets_info->_send(websockets_info, buf, len, type);
}

static void websockets_client_exit(struct websocket_struct *websockets_info)
{
    websockets_info->_exit(websockets_info);
}


/*******************************************************************************
*   Websocket Client.c
*   Just one example for test
*******************************************************************************/
static int my_fwrite(FILE *file, void *buf, u32 size)
{
    int ret = fwrite(buf, size, 1, file);
    return ret;
}
static void websockets_client_main_thread(void *priv)
{
    int err;
    char mode = WEBSOCKET_MODE;
    struct websocket_struct *websockets_info;
    char url[256];
    snprintf(url, sizeof(url), OBJ_URL);
    MY_LOG_D(">>>>>url %s \n", url);
    const char *origin_str = "http://coolaf.com";
    /* 0 . malloc buffer */
    websockets_info = my_malloc(sizeof(struct websocket_struct));
    if (!websockets_info) {
        return;
    }
    /* 1 . register */
    websockets_client_reg(websockets_info, mode);

    /* 2 . init */
    err = websockets_client_init(websockets_info, (u8 *)url, origin_str, NULL);
    if (FALSE == err) {
        MY_LOG_E("  . ! Cilent websocket init error !!!\r\n");
        goto exit_ws;
    }

    /* 3 . hanshack */
    err = websockets_client_handshack(websockets_info);
    if (FALSE == err) {
        MY_LOG_E("  . ! Handshake error !!!\r\n");
        goto exit_ws;
    }
    MY_LOG_D(" . Handshake success \r\n");
    /* rtc_rec_start(); */
    /* 4 . CreateThread */
    err = os_task_create(websockets_info->_heart_thread,
                         websockets_info,
                         19,
                         512,
                         0,
                         "websocket_client_heart");
    if (err == 0) {
        websockets_info->ping_thread_id = 1;
    } else {

        websockets_info->ping_thread_id = 0;
    }
    err = os_task_create(websockets_info->_recv_thread,
                         websockets_info,
                         18,
                         512,
                         0,
                         "websocket_client_recv");
    if (err == 0) {
        websockets_info->recv_thread_id = 1;
    } else {

        websockets_info->recv_thread_id = 0;
    }
    websockets_sleep(1000);
    u8 send_buf[40];

    char *question = "[T]:你是谁？";

    while (1) {
        err = websockets_client_send(websockets_info, (u8 *)question, strlen(question), WCT_BINDATA);
        if (false == err) {
            MY_LOG_E("  . ! send err !!!\r\n");
            goto exit_ws;
        }
        websockets_sleep(10);
    }

#if 0
    while (1) {
        int rlen = cbuf_read(&rc.cbuf, send_buf, 40);
        if (rlen != 40) {
            cbuf_read(&rc.cbuf, send_buf, rlen);
        }
        err = websockets_client_send(websockets_info, send_buf, rlen, WCT_BINDATA);
        if (false == err) {
            MY_LOG_E("  . ! send err !!!\r\n");
            goto exit_ws;
        }
        printf(">>>>>>>cbuf read \n");
        put_buf(send_buf, rlen);
        websockets_sleep(3000);
    }
#endif
#if 0
    websockets_sleep(1000);
    FILE *fp = fopen("storage/sd0/C/ze.bin", "r");
    if (fp == NULL) {
        MY_LOG_E("Error: Failed to open record  file\r\n");
        goto exit_ws;
    }
    u8 buffer[40];
    while (1) {
        size_t bytes_read = fread(buffer, 40, 1, fp);
        if (bytes_read == 0) {
            MY_LOG_D("file sent completely.\r\n");
            break;
        }
        err = websockets_client_send(websockets_info, buffer, bytes_read, WCT_BINDATA);
        if (false == err) {
            MY_LOG_E("  . ! send err !!!\r\n");
            fclose(fp);
            goto exit_ws;
        }
        websockets_sleep(10);
    }
    fclose(fp);
#endif

exit_ws:
    /* 6 . exit */
    if (websockets_info->ping_thread_id) {
        websockets_info->ping_kill_flag = 1;
    }
    if (websockets_info->recv_thread_id) {
        websockets_info->recv_kill_flag = 1;
    }
    while (websockets_info->recv_kill_flag || websockets_info->ping_kill_flag) {
        os_time_dly(10);
    }

    /*
    if(fp)
    {
        fclose(fp);
    }
    */
    websockets_client_exit(websockets_info);
    my_free(websockets_info);
    int msg[5];
    msg[0] = (int)task_kill_callback;
    msg[1] = 1;
    msg[2] = (int)os_current_task();
    err = os_taskq_post_type("app_core", Q_CALLBACK, 3, msg);
    os_time_dly(-1);
}


static void task_kill_callback(char *buf)
{
    MY_LOG_I("[msg]>>>>>>>>>>>*buf=%s", buf);
    task_kill(buf);
}

void my_rtc_websocket_client_thread_create(void)
{
    /*
    fp = fopen("storage/sd0/C/music.bin", "wb+");
    if (!fp) {
        printf("!!! Failed to open file: %s\n", save_path);
    }
    */
    os_task_create(websockets_client_main_thread,
                   NULL,
                   15,
                   512 * 13,
                   0,
                   "websockets_client_main");
}
#endif
