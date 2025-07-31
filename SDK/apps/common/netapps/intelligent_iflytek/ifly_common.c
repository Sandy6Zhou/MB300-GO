#include "ifly_common.h"


void str_remove_quote(char *str, int len)
{
    int i, j;
    for (i = 0, j = 0; i < len; i++) {
        if (str[i] != '\"') {
            str[j++] = str[i];
        }
    }
    str[j] = '\0';
}
