#include "rtc_common.h"

#if INTELLIGENT_RTC
// 辅助函数：检查字符是否为安全字符（无需编码）
static bool is_safe_char(char c)
{
    return isalnum((unsigned char)c) ||
           c == '-' || c == '_' || c == '.' || c == '~';
}

// URI编码函数
char *UriEncode(const char *str)
{
    if (!str) {
        return NULL;
    }

    // 分配足够空间：每个字符最多编码为3字符（%XX）
    size_t len = strlen(str);
    char *encoded = my_malloc(3 * len + 1);
    if (!encoded) {
        return NULL;
    }

    char *ptr = encoded;

    for (; *str; str++) {
        unsigned char c = *str;

        if (is_safe_char(c)) {
            *ptr++ = c;
        } else if (c == ' ') {
            strcpy(ptr, "%20");
            ptr += 3;
        } else {
            sprintf(ptr, "%%%02X", c);
            ptr += 3;
        }
    }

    *ptr = '\0';
    return encoded;
}

// 解析查询参数结构体
typedef struct {
    char *key;
    char *value;
    bool has_value;
} QueryParam;

// 交换两个QueryParam结构体
void swap_params(QueryParam *a, QueryParam *b)
{
    QueryParam temp = *a;
    *a = *b;
    *b = temp;
}

// 实现快速排序的分区函数
static int partition(QueryParam *params, int low, int high)
{
    // 选择中间元素作为基准值
    int mid = low + (high - low) / 2;
    const char *pivot = params[mid].key;

    // 将基准值移到最右边
    swap_params(&params[mid], &params[high]);

    int i = low - 1;
    for (int j = low; j < high; j++) {
        // 如果当前元素小于或等于基准值
        if (strcmp(params[j].key, pivot) <= 0) {
            i++;
            swap_params(&params[i], &params[j]);
        }
    }

    // 将基准值移到正确位置
    swap_params(&params[i + 1], &params[high]);
    return i + 1;
}

// 手动实现快速排序
void quicksort_params(QueryParam *params, int low, int high)
{
    // 当low < high时递归
    if (low < high) {
        // 获取分区点
        int pi = partition(params, low, high);

        // 递归排序左半部分和右半部分
        quicksort_params(params, low, pi - 1);
        quicksort_params(params, pi + 1, high);
    }
}

// 手动实现的排序函数入口
void sort_params(QueryParam *params, int count)
{
    quicksort_params(params, 0, count - 1);
}

// 主函数：解析URL并生成CanonicalQueryString
char *get_canonical_querystring(const char *url)
{
    // 1. 提取查询字符串部分
    const char *query_start = strchr(url, '?');
    if (!query_start) {
        return strdup("");    // 没有查询字符串
    }
    query_start++;  // 跳过'?'字符

    // 2. 创建可修改的副本
    char *query_copy = strdup(query_start);
    if (!query_copy) {
        return NULL;
    }

    // 3. 分割查询参数
    int max_params = 20;
    QueryParam *params = my_calloc(max_params, sizeof(QueryParam));
    if (!params) {
        my_free(query_copy);
        return NULL;
    }

    int count = 0;
    char *token = strtok(query_copy, "&");

    while (token && count < max_params) {
        // 查找键值分隔符
        char *eq_pos = strchr(token, '=');

        if (eq_pos) {
            // key=value 形式
            *eq_pos = '\0';
            char *value = eq_pos + 1;

            // 忽略authorization参数
            if (strcmp(token, "authorization") == 0) {
                token = strtok(NULL, "&");
                continue;
            }

            // 存储键值对
            params[count].key = strdup(token);
            params[count].value = strdup(value);
            params[count].has_value = true;
        } else {
            // 只有key没有value
            if (strcmp(token, "authorization") == 0) {
                token = strtok(NULL, "&");
                continue;
            }

            params[count].key = strdup(token);
            params[count].value = NULL;
            params[count].has_value = false;
        }

        count++;
        token = strtok(NULL, "&");
    }

    my_free(query_copy);

    // 4. 对参数按key进行排序
    if (count > 0) {
        sort_params(params, count);
    }

    // 5. 计算最终字符串长度
    size_t total_len = 1;  // 末尾的'\0'
    for (int i = 0; i < count; i++) {
        char *enc_key = UriEncode(params[i].key);
        total_len += strlen(enc_key) + 1;  // key + '='
        my_free(enc_key);

        if (params[i].has_value && params[i].value) {
            char *enc_val = UriEncode(params[i].value);
            total_len += strlen(enc_val);
            my_free(enc_val);
        }
    }
    total_len += (count - 1);  // '&'分隔符

    // 6. 构建最终字符串
    char *result = my_malloc(total_len);
    if (!result) {
        // 清理内存
        for (int i = 0; i < count; i++) {
            my_free(params[i].key);
            if (params[i].value) {
                my_free(params[i].value);
            }
        }
        my_free(params);
        return NULL;
    }

    char *ptr = result;
    for (int i = 0; i < count; i++) {
        if (i > 0) {
            *ptr++ = '&';
        }

        char *enc_key = UriEncode(params[i].key);
        char *enc_val = NULL;
        if (params[i].has_value && params[i].value) {
            enc_val = UriEncode(params[i].value);
        }

        size_t key_len = strlen(enc_key);
        memcpy(ptr, enc_key, key_len);
        ptr += key_len;
        *ptr++ = '=';

        if (enc_val) {
            size_t val_len = strlen(enc_val);
            memcpy(ptr, enc_val, val_len);
            ptr += val_len;
            my_free(enc_val);
        }

        my_free(enc_key);
        my_free(params[i].key);
        if (params[i].value) {
            my_free(params[i].value);
        }
    }
    *ptr = '\0';

    my_free(params);
    return result;
}

#if 0
int encode_test()
{
    const char *url = "https://bos.cn-n1.baidubce.com/example?text&text1=测试&text10=test";
    MY_LOG_D("Original URL: %s\n", url);
    MY_LOG_D("Expected: text10=test&text1=%%E6%%B5%%8B%%E8%%AF%%95&text=\n");
    char *cqs = get_canonical_querystring(url);
    if (cqs) {
        MY_LOG_D("CanonicalQueryString: %s\n", cqs);
        my_free(cqs);
    } else {
        MY_LOG_D("Failed to generate CanonicalQueryString\n");
    }
    // 测试用例2：包含authorization的情况
    const char *url2 = "https://example.com?b=value2&a=value1&authorization=secret&c=value3";
    char *cqs2 = get_canonical_querystring(url2);
    MY_LOG_D("\nTest 2: Authorization filter\n");
    MY_LOG_D("URL: %s\n", url2);
    MY_LOG_D("CanonicalQueryString: %s\n", cqs2 ? cqs2 : "N/A");
    if (cqs2) {
        my_free(cqs2);
    }

    return 0;
}
#endif
#endif
