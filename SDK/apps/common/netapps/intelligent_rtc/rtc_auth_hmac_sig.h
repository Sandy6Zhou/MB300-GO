#ifndef _RTC_AUTH_HMAC_SIG_H_
#define _RTC_AUTH_HMAC_SIG_H_


#include "system/includes.h"

extern int hmac_main();

extern char *HMAC_SHA256_HEX(const char *key, const char *msg);
#endif

