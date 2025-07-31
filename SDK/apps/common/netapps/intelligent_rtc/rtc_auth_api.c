#include "mbedtls_3_4_0/mbedtls/md.h"
#include "http/http_cli.h"
#include "rtc_common.h"
#include "os/os_api.h"

#if  INTELLIGENT_RTC
extern int ntp_client_get_time(const char *host);

char *rtc_get_auth_authorization()
{
    char stringprefix[256];
    char *SigningKey = NULL;
    char *CanonicalURI = NULL;
    char *CanonicalQueryString = NULL;
    char *CanonicalHeaders = NULL;
    char *SignedHeaders = NULL;
    char *signature = NULL;
    char CanonicalRequest[256];
    char *authorization = NULL;

    //1、AuthStringPrefix
    char timestamp[256];
    int ntp = 0;
    ntp = ntp_client_get_time("s2c.time.edu.cn");
    if (ntp == -1) {
        MY_LOG_E("get ntp error!!\n");
        return NULL;
    }
    struct sys_time curtime;
    my_get_sys_time(&curtime);
    curtime.hour -= 8;
    my_get_timestamp(&curtime, timestamp, sizeof(timestamp));
    sprintf(stringprefix, "%s/%s/%s/%s", "bce-auth-v1", RTC_AK, timestamp, RTC_SEC);
    MY_LOG_D(">>>>>stringprefix %s\n", stringprefix);


    //3、CanonicalRequest
    //HTTP Method + "\n" + CanonicalURI + "\n" + CanonicalQueryString + "\n" + CanonicalHeaders
    //3.1、Canonicaluri
    CanonicalURI  = UriEncodeExceptSlash(RTC_URI);
    MY_LOG_D("CanonicalURI: %s\n", CanonicalURI);



    //3.2、CanonicalQueryString
    /* const char *url = "https://rtc-aiagent.baidubce.com/api/v1/aiagent/generateAIAgentCall"; */
    /* CanonicalQueryString = get_canonical_querystring(url); */
    /* printf("CanonicalQueryString: %s\n", CanonicalQueryString); */


    //3.3、CanonicalHeaders
    Header example1[] = {
        {"Host", RTC_HOST},
    };
    generate_canonical_headers(example1, 1, &CanonicalHeaders, &SignedHeaders);
    MY_LOG_D("CanonicalHeaders:\n%s\n", CanonicalHeaders);
    MY_LOG_D("SignedHeaders: %s\n\n", SignedHeaders);


    sprintf(CanonicalRequest, "%s\n%s\n%s\n%s", RTC_METHOD, CanonicalURI, "", CanonicalHeaders);
    MY_LOG_D(">>>CanonicalRequest %s \n", CanonicalRequest);

    //2、SigningKey
    SigningKey = HMAC_SHA256_HEX(RTC_SK, stringprefix);
    MY_LOG_D("SigningKey: %s\n", SigningKey);

    //4、signature
    signature = HMAC_SHA256_HEX(SigningKey, CanonicalRequest);
    MY_LOG_D("signature: %s\n", signature);

    //5、Authorization=signingkey + signature
    authorization = my_malloc(256);
    snprintf(authorization, 256, "%s/host/%s", stringprefix, signature);
    MY_LOG_D(">>>>>>>>authorization :%s \n", authorization);


    my_free(SigningKey);
    if (CanonicalURI  != NULL) {
        MY_LOG_D("Encoded: %s\n", CanonicalURI);
        my_free(CanonicalURI);
    }
    if (CanonicalQueryString) {
        my_free(CanonicalQueryString);
    }
    my_free(CanonicalHeaders);
    my_free(SignedHeaders);
    my_free(signature);
    return authorization;
}

void rtc_get_auth_url()
{
    char *authorization = rtc_get_auth_authorization();
    my_free(authorization);
}






#if 0
#define HTTP_DEMO_URL  "https://rtc-aiagent.baidubce.com/api/v1/aiagent/generateAIAgentCall"
#define HTTP_DEMO_DATA "{\"app_id\":\"apprfcf1f27z5t3\"}"
#define LOG_HTTP_HEAD_OPTION        \
    "POST RTC_URI HTTP/1.1\r\n"\
    "Authorization: %s\r\n"\
    "Content-type: application/json\r\n"\
    "Host: rtc-aiagent.baidubce.com\r\n\r\n"\


static void http_test_task()
{
    int ret = 0;
    char *authorization = rtc_get_auth_authorization();
    if (!authorization) {
        MY_LOG_D("Authorization failed\n");
        return;
    }
    MY_LOG_D(">>>>>>>>Authorization: %s\n", authorization);
    http_body_obj http_body_buf;
    char *user_head_buf = NULL;
    httpcli_ctx *ctx = (httpcli_ctx *)my_calloc(1, sizeof(httpcli_ctx));
    if (!ctx) {
        MY_LOG_E("calloc faile\n");
        return;
    }
    user_head_buf = my_malloc(512);
    if (!user_head_buf) {
        MY_LOG_E("%s %d malloc fail", __func__, __LINE__);
        return;
    }
    memset(user_head_buf, 0, 512);
    snprintf(user_head_buf, 512, LOG_HTTP_HEAD_OPTION, authorization);
    MY_LOG_D(">>>>>>>>user_head_buf %s \n", user_head_buf);
    memset(&http_body_buf, 0x0, sizeof(http_body_obj));
    http_body_buf.recv_len = 0;
    http_body_buf.buf_len = 4 * 1024;
    http_body_buf.buf_count = 1;
    http_body_buf.p = (char *) my_malloc(http_body_buf.buf_len * http_body_buf.buf_count);
    if (!http_body_buf.p) {
        my_free(ctx);
        return;
    }
    ctx->url = HTTP_DEMO_URL;
    ctx->timeout_millsec = 5000;
    ctx->priv = &http_body_buf;
    ctx->connection = "close";
    MY_LOG_D("http post test start\n\n");
    ctx->data_format = "application/json";
    ctx->post_data = HTTP_DEMO_DATA;
    ctx->data_len = strlen(HTTP_DEMO_DATA);
    ctx->user_http_header = user_head_buf;	//添加该参数, 则自定义申请头部内容
    ret = httpcli_post(ctx);
    if (ret != HERROR_OK) {
        MY_LOG_E("http client test faile\n");
    } else {
        if (http_body_buf.recv_len > 0) {
            MY_LOG_I("\nReceive %d Bytes from (%s)\n", http_body_buf.recv_len, HTTP_DEMO_URL);
            MY_LOG_I("%s\n", http_body_buf.p);
        }
    }
    //关闭连接
    httpcli_close(ctx);
    if (http_body_buf.p) {
        my_free(http_body_buf.p);
        http_body_buf.p = NULL;
    }
    if (authorization) {
        my_free(authorization);
        authorization = NULL;
    }
    if (user_head_buf) {
        my_free(user_head_buf);
        user_head_buf = NULL;
    }
    if (ctx) {
        my_free(ctx);
        ctx = NULL;
    }
}

static void http_test_start(void *priv)
{
    while (1) {
        http_test_task();
        os_time_dly(200);
    }
}
void http_test_main()
{
    os_task_create(http_test_start, NULL, 3, 512, 0, "http_test_start");
}
#endif
#endif
