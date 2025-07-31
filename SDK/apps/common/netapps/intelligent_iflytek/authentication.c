#include "mbedtls_3_4_0/mbedtls/md.h"
#include "mbedtls_3_4_0/mbedtls/base64.h"
#include "authentication.h"
#include "my_platform_time.h"
#include "ifly_common.h"

#if TCFG_IFLYTEK_ENABLE
#define API_SECRET   			TCFG_IFLYTEK_APP_SECRET
#define API_KEY      			TCFG_IFLYTEK_APP_KEY

// 鉴权信息结构体
struct auth_info_t {
    char sig_ori[100];
    char sig_hmac[100];
    char sig_hmac_base64[50];
    char author_ori[200];
    char author[300];
    char date_str[50];
    struct sys_time curtime;
    struct tm pt;
};

extern int ntp_client_get_time(const char *host);
extern size_t strftime_2(char *ptr, size_t maxsize, const char *format, const struct tm *timeptr);

static char dec2hex(short int c)
{
    if (0 <= c && c <= 9) {
        return c + '0';
    } else if (10 <= c && c <= 15) {
        return c + 'A' - 10;
    } else {
        return -1;
    }
}

static void urlencode(char url[])
{
    int i = 0;
    int len = strlen(url);
    int res_len = 0;
    char res[100];
    for (i = 0; i < len; ++i) {
        char c = url[i];
        if (('0' <= c && c <= '9') ||
            ('a' <= c && c <= 'z') ||
            ('A' <= c && c <= 'Z') ||
            c == '/' || c == '.') {
            res[res_len++] = c;
        } else {
            int j = (short int)c;
            if (j < 0) {
                j += 256;
            }
            int i1, i0;
            i1 = j / 16;
            i0 = j - i1 * 16;
            res[res_len++] = '%';
            res[res_len++] = dec2hex(i1);
            res[res_len++] = dec2hex(i0);
        }
    }
    res[res_len] = '\0';
    strcpy(url, res);
}



char *ifly_authentication(char *host_name, char *host, char *path)
{
    //初始化
    struct auth_info_t *auth = zalloc(sizeof(struct auth_info_t));
    char *url_xf = zalloc(400);

    //获取网络时间
    int ntp = ntp_client_get_time("s2c.time.edu.cn");
    if (ntp == -1) {
        MY_LOG_E("get ntp error!!\n");
        return NULL;
    }
    my_get_sys_time(&(auth->curtime));
    auth->pt.tm_year = auth->curtime.year - 1900;
    auth->pt.tm_mon = auth->curtime.month - 1;
    auth->pt.tm_mday = auth->curtime.day;
    auth->pt.tm_hour = auth->curtime.hour - 8;
    auth->pt.tm_min = auth->curtime.min;
    auth->pt.tm_sec = auth->curtime.sec;
    my_strftime_2(auth->date_str, 40, "", &auth->pt);
    MY_LOG_I("%s\n", auth->date_str);

    //拼接sig原始字符
    sprintf(auth->sig_ori, "%s%s\ndate: %s\n%s", "host: ", host, auth->date_str, path);
    //通过hmac_sha256加密
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *info;
    mbedtls_md_init(&ctx);
    info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_setup(&ctx, info, 1);
    mbedtls_md_hmac_starts(&ctx, (unsigned char *)API_SECRET, strlen(API_SECRET));
    mbedtls_md_hmac_update(&ctx, (unsigned char *)auth->sig_ori, strlen(auth->sig_ori));
    mbedtls_md_hmac_finish(&ctx, (unsigned char *)auth->sig_hmac);
    mbedtls_md_free(&ctx);
    //进行base64编码
    int res;
    mbedtls_base64_encode((unsigned char *)auth->sig_hmac_base64, 50, (size_t *)&res, (unsigned char *)auth->sig_hmac, strlen(auth->sig_hmac));
    //拼接authorization_ori字符串
    strcat(auth->author_ori, "api_key=\"");
    strcat(auth->author_ori, API_KEY);
    strcat(auth->author_ori, "\",");
    strcat(auth->author_ori, "algorithm=\"hmac-sha256\",");
    strcat(auth->author_ori, "headers=\"host date request-line\",");
    strcat(auth->author_ori, "signature=\"");
    strcat(auth->author_ori, auth->sig_hmac_base64);
    strcat(auth->author_ori, "\"");
    //对autho_ori进行base64编码
    int author_res;
    mbedtls_base64_encode((unsigned char *)auth->author, 300, (size_t *)&author_res, (unsigned char *)auth->author_ori, strlen(auth->author_ori));
    //拼接url
    strcat(url_xf, host_name);
    strcat(url_xf, "?");
    urlencode(auth->date_str);
    strcat(url_xf, "authorization=");
    strcat(url_xf, auth->author);
    strcat(url_xf, "&date=");
    strcat(url_xf, auth->date_str);
    strcat(url_xf, "&host=");
    strcat(url_xf, host);
    MY_LOG_I("url_xf:%s\n", url_xf);
    free(auth);
    return url_xf;
}

#endif
