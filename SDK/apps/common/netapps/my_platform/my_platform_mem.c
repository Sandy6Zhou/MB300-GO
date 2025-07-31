#include "my_platform_mem.h"

void *my_malloc(size_t size)
{
    void *p = malloc(size);
    return p;
}

void my_free(void *pv)
{
    if (pv == NULL) {
        return;
    }
    free(pv);
}

void *my_calloc(unsigned long count, unsigned long size)
{
    size_t total = count * size;
    void *p = my_malloc(total);
    if (p) {
        memset(p, 0, total);
    }
    return p;
}

void *_calloc_r(struct _reent *r, size_t a, size_t b)
{
    return calloc(a, b);
}

_WEAK_
void *calloc(unsigned long count, unsigned long size)
{
    void *p;

    p = malloc(count * size);
    if (p) {
        memset(p, 0, count * size);
    }
    return p;
}


_WEAK_
void *realloc(void *ptr, size_t size)
{
    free(ptr);
    ptr = malloc(size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}
