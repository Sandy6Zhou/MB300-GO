#include "rtc_common.h"

#if  INTELLIGENT_RTC
char *UriEncodeExceptSlash(const char *str)
{
    if (str == NULL) {
        return NULL;
    }

    // 定义非保留字符集（RFC 3986）
    const char *unreserved = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";

    // 计算编码后需要的最大长度（每个字节最多编码成3个字符）
    size_t len = strlen(str);
    char *result = (char *)my_malloc(3 * len + 1); // +1 for null terminator
    if (result == NULL) {
        return NULL;
    }

    const unsigned char *p = (const unsigned char *)str;
    char *dst = result;

    while (*p != '\0') {
        // 判断是否为非保留字符或斜杠
        if ((*p >= 'A' && *p <= 'Z') ||
            (*p >= 'a' && *p <= 'z') ||
            (*p >= '0' && *p <= '9') ||
            (*p == '-' || *p == '.' || *p == '_' || *p == '~' || *p == '/')) {
            *dst++ = *p;
        }
        // 对其它字符进行百分号编码
        else {
            *dst++ = '%';
            // 高4位转十六进制
            *dst++ = "0123456789ABCDEF"[(*p >> 4) & 0x0F];
            // 低4位转十六进制
            *dst++ = "0123456789ABCDEF"[*p & 0x0F];
        }
        p++;
    }
    *dst = '\0'; // 添加字符串结束符

    return result;
}

#if 0
int UriEncodeExceptSlash_demo()
{
    const char *input = "Hello World!/~escape";
    char *encoded = UriEncodeExceptSlash(input);
    if (encoded != NULL) {
        MY_LOG_D("Encoded: %s\n", encoded); // 输出: Hello%20World%21/~escape
        my_free(encoded);
    }
    return 0;
}

#endif
#endif
