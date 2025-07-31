#ifndef _MY_PLATFORM_MEM_H
#define _MY_PLATFORM_MEM_H

#include "system/includes.h"
#include "my_platform_common.h"

extern void *my_malloc(size_t size);
extern void *my_calloc(unsigned long count, unsigned long size);
extern void my_free(void *pv);
#endif // _MY_PLATFORM_MEM_H
