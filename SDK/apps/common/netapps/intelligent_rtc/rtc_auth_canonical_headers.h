#ifndef _RTC_AUTH_CANONICAL_HEADERS_H_
#define _RTC_AUTH_CANONICAL_HEADERS_H_

#include  "system/includes.h"




// 定义Header结构体
typedef struct {
    char *name;
    char *value;
} Header;



extern void generate_canonical_headers(Header *headers, int count,
                                       char **canonical_headers,
                                       char **signed_headers);
#endif
