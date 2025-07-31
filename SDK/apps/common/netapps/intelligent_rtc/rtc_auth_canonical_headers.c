#include "rtc_common.h"
//HTTP Method + "\n" + CanonicalURI + "\n" + CanonicalQueryString + "\n" + CanonicalHeaders
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#if  INTELLIGENT_RTC

// 定义处理后的Header结构体
typedef struct {
    char *lower_name;  // 小写化的Header名
    char *trimmed_value; // 去除首尾空格的Header值
    char *encoded_name;  // URI编码后的Header名
    char *encoded_value; // URI编码后的Header值
    char *combined;    // 组合字符串"encoded_name:encoded_value"
} ProcessedHeader;

// 判断字符是否为安全字符（不需要编码）
bool is_safe_char(unsigned char c)
{
    return isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
}

// URI编码函数
static char *uri_encode(const char *str)
{
    if (!str) {
        return NULL;
    }

    // 计算编码后所需长度
    size_t len = strlen(str);
    size_t new_len = 0;
    for (size_t i = 0; i < len; i++) {
        new_len += is_safe_char(str[i]) ? 1 : 3;
    }

    char *encoded = my_malloc(new_len + 1);
    if (!encoded) {
        return NULL;
    }

    size_t pos = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = str[i];
        if (is_safe_char(c)) {
            encoded[pos++] = c;
        } else {
            sprintf(encoded + pos, "%%%02X", c);
            pos += 3;
        }
    }
    encoded[pos] = '\0';
    return encoded;
}

// 转换为小写字符串
static char *to_lower(const char *str)
{
    if (!str) {
        return NULL;
    }
    char *lower = strdup(str);
    for (char *p = lower; *p; p++) {
        *p = tolower((unsigned char) * p);
    }
    return lower;
}

// 去除字符串首尾空格
static char *trim(const char *str)
{
    if (!str) {
        return NULL;
    }

    // 跳过前导空格
    size_t start = 0;
    while (isspace((unsigned char)str[start])) {
        start++;
    }

    // 全空格情况
    size_t len = strlen(str);
    if (start == len) {
        return strdup("");
    }

    // 找到后缀空格
    size_t end = len - 1;
    while (end > start && isspace((unsigned char)str[end])) {
        end--;
    }

    // 复制结果
    char *trimmed = my_malloc(end - start + 2);
    if (!trimmed) {
        return NULL;
    }

    size_t pos = 0;
    for (size_t i = start; i <= end; i++) {
        trimmed[pos++] = str[i];
    }
    trimmed[pos] = '\0';
    return trimmed;
}

// 比较函数：用于按combined字符串排序
static int cmp_combined(const void *a, const void *b)
{
    const ProcessedHeader *ha = *(const ProcessedHeader **)a;
    const ProcessedHeader *hb = *(const ProcessedHeader **)b;
    return strcmp(ha->combined, hb->combined);
}

// 比较函数：用于按Header名排序
static int cmp_header_name(const void *a, const void *b)
{
    const ProcessedHeader *ha = *(const ProcessedHeader **)a;
    const ProcessedHeader *hb = *(const ProcessedHeader **)b;
    return strcmp(ha->lower_name, hb->lower_name);
}

// 生成CanonicalHeaders和SignedHeaders的主函数
void generate_canonical_headers(Header *headers, int count,
                                char **canonical_headers,
                                char **signed_headers)
{
    ProcessedHeader **processed = my_malloc(count * sizeof(ProcessedHeader *));
    if (!processed) {
        return;
    }

    int valid_count = 0;

    // 1. 处理每个Header
    for (int i = 0; i < count; i++) {
        // 转换为小写名称
        char *lower_name = to_lower(headers[i].name);
        if (!lower_name) {
            continue;
        }

        // 去除Header值首尾空格
        char *trimmed_value = trim(headers[i].value);
        if (!trimmed_value) {
            my_free(lower_name);
            continue;
        }

        // 空值则跳过
        if (strlen(trimmed_value) == 0) {
            my_free(lower_name);
            my_free(trimmed_value);
            continue;
        }

        // URI编码名称和值
        char *enc_name = uri_encode(lower_name);
        char *enc_value = uri_encode(trimmed_value);

        if (!enc_name || !enc_value) {
            my_free(lower_name);
            my_free(trimmed_value);
            my_free(enc_name);
            my_free(enc_value);
            continue;
        }

        // 组合编码结果
        size_t combined_len = strlen(enc_name) + strlen(enc_value) + 2;
        char *combined = my_malloc(combined_len);
        if (!combined) {
            my_free(lower_name);
            my_free(trimmed_value);
            my_free(enc_name);
            my_free(enc_value);
            continue;
        }
        sprintf(combined, "%s:%s", enc_name, enc_value);

        // 保存处理后的Header
        ProcessedHeader *ph = my_malloc(sizeof(ProcessedHeader));
        ph->lower_name = lower_name;
        ph->trimmed_value = trimmed_value;
        ph->encoded_name = enc_name;
        ph->encoded_value = enc_value;
        ph->combined = combined;

        processed[valid_count++] = ph;
    }

    // 2. 按组合字符串排序(用于CanonicalHeaders)
    qsort(processed, valid_count, sizeof(ProcessedHeader *), cmp_combined);

    // 生成CanonicalHeaders字符串
    size_t canon_len = 0;
    for (int i = 0; i < valid_count; i++) {
        canon_len += strlen(processed[i]->combined) + 1; // +1 for \n or \0
    }

    *canonical_headers = my_malloc(canon_len);
    if (*canonical_headers) {
        char *p = *canonical_headers;
        for (int i = 0; i < valid_count; i++) {
            int len = sprintf(p, "%s%s",
                              (i > 0) ? "\n" : "",
                              processed[i]->combined);
            p += len;
        }
    }

    // 3. 按Header名称排序(用于SignedHeaders)
    qsort(processed, valid_count, sizeof(ProcessedHeader *), cmp_header_name);

    // 生成SignedHeaders字符串
    size_t signed_len = 0;
    for (int i = 0; i < valid_count; i++) {
        signed_len += strlen(processed[i]->lower_name) + 1; // +1 for ; or \0
    }

    *signed_headers = my_malloc(signed_len);
    if (*signed_headers) {
        char *p = *signed_headers;
        for (int i = 0; i < valid_count; i++) {
            int len = sprintf(p, "%s%s",
                              (i > 0) ? ";" : "",
                              processed[i]->lower_name);
            p += len;
        }
    }

    // 释放内存
    for (int i = 0; i < valid_count; i++) {
        my_free(processed[i]->lower_name);
        my_free(processed[i]->trimmed_value);
        my_free(processed[i]->encoded_name);
        my_free(processed[i]->encoded_value);
        my_free(processed[i]->combined);
        my_free(processed[i]);
    }
    my_free(processed);
}

#if 0
int header_demo()
{
    // 示例1数据
    Header example1[] = {
        {"Host", "bj.bcebos.com"},
        {"Date", "Mon, 27 Apr 2015 16:23:49 +0800"},
        {"Content-Type", "text/plain"},
        {"Content-Length", "8"},
        {"Content-Md5", "NFzcPqhviddjRNnSOGo4rw=="}
    };

    // 示例2数据
    Header example2[] = {
        {"Host", "bj.bcebos.com"},
        {"x-bce-meta-data", "my meta data"},
        {"x-bce-meta-data-tag", "description"}
    };

    char *canonical = NULL;
    char *signed_headers = NULL;

    MY_LOG_D("Example 1:\n");
    generate_canonical_headers(example1, 5, &canonical, &signed_headers);
    MY_LOG_D("CanonicalHeaders:\n%s\n", canonical);
    MY_LOG_D("SignedHeaders: %s\n\n", signed_headers);
    my_free(canonical);
    my_free(signed_headers);

    MY_LOG_D("Example 2:\n");
    generate_canonical_headers(example2, 3, &canonical, &signed_headers);
    MY_LOG_D("CanonicalHeaders:\n%s\n", canonical);
    MY_LOG_D("SignedHeaders: %s\n", signed_headers);
    my_free(canonical);
    my_free(signed_headers);

    return 0;
}
#endif
#endif
