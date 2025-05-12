#include <math.h>
#include <float.h>
#include <limits.h>
#include <ctype.h>
#include "my_cjson.h"

static const char *ep;

const char *my_cJSON_GetErrorPtr(void)
{
    return ep;
}

static int my_cJSON_strcasecmp(const char *s1, const char *s2)
{
    if (!s1) {
        return (s1 == s2) ? (0) : (1);
    }
    if (!s2) {
        return 1;
    }
    for (; tolower(*s1) == tolower(*s2); ++s1, ++s2) {
        if (*s1 == 0) {
            return 0;
        }
    }
    return tolower(*(const unsigned char *)s1) - tolower(*(const unsigned char *)s2);
}

static void *(*my_cJSON_malloc)(size_t sz) = malloc;
static void (*my_cJSON_free)(void *ptr) = free;

static char *my_cJSON_strdup(const char *str)
{
    size_t len;
    char *copy;

    len = strlen(str) + 1;
    if (!(copy = (char *)my_cJSON_malloc(len))) {
        return 0;
    }
    memcpy(copy, str, len);
    return copy;
}

void my_cJSON_InitHooks(my_cJSON_Hooks *hooks)
{
    if (!hooks) { /* Reset hooks */
        my_cJSON_malloc = malloc;
        my_cJSON_free = free;
        return;
    }

    my_cJSON_malloc = (hooks->malloc_fn) ? hooks->malloc_fn : malloc;
    my_cJSON_free	 = (hooks->free_fn) ? hooks->free_fn : free;
}

/* Internal constructor. */
static my_cJSON *my_cJSON_New_Item(void)
{
    my_cJSON *node = (my_cJSON *)my_cJSON_malloc(sizeof(my_cJSON));
    if (node) {
        memset(node, 0, sizeof(my_cJSON));
    }
    return node;
}

/* Delete a my_cJSON structure. */
void my_cJSON_Delete(my_cJSON *c)
{
    my_cJSON *next;
    while (c) {
        next = c->next;
        if (!(c->type & my_cJSON_IsReference) && c->child) {
            my_cJSON_Delete(c->child);
        }
        if (!(c->type & my_cJSON_IsReference) && c->valuestring) {
            my_cJSON_free(c->valuestring);
        }

        if (c->string) {
            my_cJSON_free(c->string);
        }

        my_cJSON_free(c);
        c = next;
    }
}

/* Parse the input text to generate a number, and populate the result into item. */
static const char *parse_number(my_cJSON *item, const char *num)
{
    double n = 0, sign = 1, scale = 0;
    int subscale = 0, signsubscale = 1;

    if (*num == '-') {
        sign = -1;
        num++;	/* Has sign? */
    }
    if (*num == '0') {
        num++;			/* is zero */
    }
    if (*num >= '1' && *num <= '9') {
        do {
            n = (n * 10.0) + (*num++ -'0');
        } while (*num >= '0' && *num <= '9');	/* Number? */
    }
    if (*num == '.' && num[1] >= '0' && num[1] <= '9') {
        num++;
        do {
            n = (n * 10.0) + (*num++ -'0');
            scale--;
        } while (*num >= '0' && *num <= '9');
    }	/* Fractional part? */
    if (*num == 'e' || *num == 'E') {	/* Exponent? */
        num++;
        if (*num == '+') {
            num++;
        } else if (*num == '-') {
            signsubscale = -1;
            num++;		/* With sign? */
        }

        while (*num >= '0' && *num <= '9') {
            subscale = (subscale * 10) + (*num++ - '0');	/* Number? */
        }
    }

    n = sign * n * pow(10.0, (scale + subscale * signsubscale));	/* number = +/- number.fraction * 10^+/- exponent */

    item->valuedouble = n;
    item->valueint = (int)n;
    item->type = my_cJSON_Number;
    return num;
}

/* Render the number nicely from the given item into a string. */
static char *print_number(my_cJSON *item)
{
    char *str;
    double d = item->valuedouble;
    if (fabs(((double)item->valueint) - d) <= DBL_EPSILON && d <= INT_MAX && d >= INT_MIN) {
        str = (char *)my_cJSON_malloc(21);	/* 2^64+1 can be represented in 21 chars. */
        if (str) {
            sprintf(str, "%d", item->valueint);
        }
    } else {
        str = (char *)my_cJSON_malloc(64);	/* This is a nice tradeoff. */
        if (str) {
            if (fabs(floor(d) - d) <= DBL_EPSILON && fabs(d) < 1.0e60) {
                sprintf(str, "%.0f", d);
            } else if (fabs(d) < 1.0e-6 || fabs(d) > 1.0e9) {
                sprintf(str, "%e", d);
            } else {
                sprintf(str, "%f", d);
            }
        }
    }
    return str;
}

static unsigned parse_hex4(const char *str)
{
    unsigned h = 0;
    if (*str >= '0' && *str <= '9') {
        h += (*str) - '0';
    } else if (*str >= 'A' && *str <= 'F') {
        h += 10 + (*str) - 'A';
    } else if (*str >= 'a' && *str <= 'f') {
        h += 10 + (*str) - 'a';
    } else {
        return 0;
    }
    h = h << 4;
    str++;

    if (*str >= '0' && *str <= '9') {
        h += (*str) - '0';
    } else if (*str >= 'A' && *str <= 'F') {
        h += 10 + (*str) - 'A';
    } else if (*str >= 'a' && *str <= 'f') {
        h += 10 + (*str) - 'a';
    } else {
        return 0;
    }
    h = h << 4;
    str++;

    if (*str >= '0' && *str <= '9') {
        h += (*str) - '0';
    } else if (*str >= 'A' && *str <= 'F') {
        h += 10 + (*str) - 'A';
    } else if (*str >= 'a' && *str <= 'f') {
        h += 10 + (*str) - 'a';
    } else {
        return 0;
    }
    h = h << 4;
    str++;

    if (*str >= '0' && *str <= '9') {
        h += (*str) - '0';
    } else if (*str >= 'A' && *str <= 'F') {
        h += 10 + (*str) - 'A';
    } else if (*str >= 'a' && *str <= 'f') {
        h += 10 + (*str) - 'a';
    } else {
        return 0;
    }
    return h;
}

/* Parse the input text into an unescaped cstring, and populate item. */
static const unsigned char firstByteMark[7] = { 0x00, 0x00, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC };
static const char *parse_string(my_cJSON *item, const char *str)
{
    const char *ptr = str + 1;
    char *ptr2;
    char *out;
    int len = 0;
    unsigned uc, uc2;
    if (*str != '\"') {
        ep = str;
        return 0;
    }	/* not a string! */

    while (*ptr != '\"' && *ptr && ++len) {
        if (*ptr++ == '\\') {
            ptr++;	/* Skip escaped quotes. */
        }
    }

    out = (char *)my_cJSON_malloc(len + 1);	/* This is how long we need for the string, roughly. */
    if (!out) {
        return 0;
    }

    ptr = str + 1;
    ptr2 = out;
    while (*ptr != '\"' && *ptr) {
        if (*ptr != '\\') {
            *ptr2++ = *ptr++;
        } else {
            ptr++;
            switch (*ptr) {
            case 'b':
                *ptr2++ = '\b';
                break;
            case 'f':
                *ptr2++ = '\f';
                break;
            case 'n':
                *ptr2++ = '\n';
                break;
            case 'r':
                *ptr2++ = '\r';
                break;
            case 't':
                *ptr2++ = '\t';
                break;
            case 'u':	 /* transcode utf16 to utf8. */
                uc = parse_hex4(ptr + 1);
                ptr += 4;	/* get the unicode char. */
                if ((uc >= 0xDC00 && uc <= 0xDFFF) || uc == 0) {
                    break;	/* check for invalid.	*/
                }
                if (uc >= 0xD800 && uc <= 0xDBFF) {	/* UTF16 surrogate pairs.	*/
                    if (ptr[1] != '\\' || ptr[2] != 'u') {
                        break;	/* missing second-half of surrogate.	*/
                    }
                    uc2 = parse_hex4(ptr + 3);
                    ptr += 6;
                    if (uc2 < 0xDC00 || uc2 > 0xDFFF) {
                        break;	/* invalid second-half of surrogate.	*/
                    }
                    uc = 0x10000 + (((uc & 0x3FF) << 10) | (uc2 & 0x3FF));
                }

                len = 4;
                if (uc < 0x80) {
                    len = 1;
                } else if (uc < 0x800) {
                    len = 2;
                } else if (uc < 0x10000) {
                    len = 3;
                }
                ptr2 += len;

                switch (len) {
                case 4:
                    *--ptr2 = ((uc | 0x80) & 0xBF);
                    uc >>= 6;
                case 3:
                    *--ptr2 = ((uc | 0x80) & 0xBF);
                    uc >>= 6;
                case 2:
                    *--ptr2 = ((uc | 0x80) & 0xBF);
                    uc >>= 6;
                case 1:
                    *--ptr2 = (uc | firstByteMark[len]);
                }
                ptr2 += len;
                break;
            default:
                *ptr2++ = *ptr;
                break;
            }
            ptr++;
        }
    }
    *ptr2 = 0;
    if (*ptr == '\"') {
        ptr++;
    }
    item->valuestring = out;
    item->type = my_cJSON_String;
    return ptr;
}

/* Render the cstring provided to an escaped version that can be printed. */
static char *print_string_ptr(const char *str)
{
    const char *ptr;
    char *ptr2, *out;
    int len = 0;
    unsigned char token;

    if (!str) {
        return my_cJSON_strdup("");
    }
    ptr = str;
    while ((token = *ptr) && ++len) {
        if (strchr("\"\\\b\f\n\r\t", token)) {
            len++;
        } else if (token < 32) {
            len += 5;
        }
        ptr++;
    }

    out = (char *)my_cJSON_malloc(len + 3);
    if (!out) {
        return 0;
    }

    ptr2 = out;
    ptr = str;
    *ptr2++ = '\"';
    while (*ptr) {
        if ((unsigned char)*ptr > 31 && *ptr != '\"' && *ptr != '\\') {
            *ptr2++ = *ptr++;
        } else {
            *ptr2++ = '\\';
            switch (token = *ptr++) {
            case '\\':
                *ptr2++ = '\\';
                break;
            case '\"':
                *ptr2++ = '\"';
                break;
            case '\b':
                *ptr2++ = 'b';
                break;
            case '\f':
                *ptr2++ = 'f';
                break;
            case '\n':
                *ptr2++ = 'n';
                break;
            case '\r':
                *ptr2++ = 'r';
                break;
            case '\t':
                *ptr2++ = 't';
                break;
            default:
                sprintf(ptr2, "u%04x", token);
                ptr2 += 5;
                break;	/* escape and print */
            }
        }
    }
    *ptr2++ = '\"';
    *ptr2++ = 0;
    return out;
}
/* Invote print_string_ptr (which is useful) on an item. */
static char *print_string(my_cJSON *item)
{
    return print_string_ptr(item->valuestring);
}

/* Predeclare these prototypes. */
static const char *parse_value(my_cJSON *item, const char *value);
static char *print_value(my_cJSON *item, int depth, int fmt);
static const char *parse_array(my_cJSON *item, const char *value);
static char *print_array(my_cJSON *item, int depth, int fmt);
static const char *parse_object(my_cJSON *item, const char *value);
static char *print_object(my_cJSON *item, int depth, int fmt);

/* Utility to jump whitespace and cr/lf */
static const char *skip(const char *in)
{
    while (in && *in && (unsigned char)*in <= 32) {
        in++;
    }
    return in;
}

/* Parse an object - create a new root, and populate. */
my_cJSON *my_cJSON_ParseWithOpts(const char *value, const char **return_parse_end, int require_null_terminated)
{
    const char *end = 0;
    my_cJSON *c = my_cJSON_New_Item();
    ep = 0;
    if (!c) {
        return 0;       /* memory fail */
    }

    end = parse_value(c, skip(value));
    if (!end) {
        my_cJSON_Delete(c);
        return 0;
    }	/* parse failure. ep is set. */

    /* if we require null-terminated JSON without appended garbage, skip and then check for a null terminator */
    if (require_null_terminated) {
        end = skip(end);
        if (*end) {
            my_cJSON_Delete(c);
            ep = end;
            return 0;
        }
    }
    if (return_parse_end) {
        *return_parse_end = end;
    }
    return c;
}
/* Default options for my_cJSON_Parse */
my_cJSON *my_cJSON_Parse(const char *value)
{
    return my_cJSON_ParseWithOpts(value, 0, 0);
}

/* Render a my_cJSON item/entity/structure to text. */
char *my_cJSON_Print(my_cJSON *item)
{
    return print_value(item, 0, 1);
}
char *my_cJSON_PrintUnformatted(my_cJSON *item)
{
    return print_value(item, 0, 0);
}

/* Parser core - when encountering text, process appropriately. */
static const char *parse_value(my_cJSON *item, const char *value)
{
    if (!value) {
        return 0;	/* Fail on null. */
    }

    if (!strncmp(value, "null", 4)) {
        item->type = my_cJSON_NULL;
        return value + 4;
    }

    if (!strncmp(value, "false", 5)) {
        item->type = my_cJSON_False;
        return value + 5;
    }

    if (!strncmp(value, "true", 4)) {
        item->type = my_cJSON_True;
        item->valueint = 1;
        return value + 4;
    }

    if (*value == '\"') {
        return parse_string(item, value);
    }

    if (*value == '-' || (*value >= '0' && *value <= '9')) {
        return parse_number(item, value);
    }

    if (*value == '[') {
        return parse_array(item, value);
    }

    if (*value == '{') {
        return parse_object(item, value);
    }

    ep = value;
    return 0;	/* failure. */
}

/* Render a value to text. */
static char *print_value(my_cJSON *item, int depth, int fmt)
{
    char *out = 0;
    if (!item) {
        return 0;
    }
    switch ((item->type) & 255) {
    case my_cJSON_NULL:
        out = my_cJSON_strdup("null");
        break;
    case my_cJSON_False:
        out = my_cJSON_strdup("false");
        break;
    case my_cJSON_True:
        out = my_cJSON_strdup("true");
        break;
    case my_cJSON_Number:
        out = print_number(item);
        break;
    case my_cJSON_String:
        out = print_string(item);
        break;
    case my_cJSON_Array:
        out = print_array(item, depth, fmt);
        break;
    case my_cJSON_Object:
        out = print_object(item, depth, fmt);
        break;
    default:
        break;
    }
    return out;
}

/* Build an array from input text. */
static const char *parse_array(my_cJSON *item, const char *value)
{
    my_cJSON *child;
    if (*value != '[') {
        ep = value;
        return 0;
    }	/* not an array! */

    item->type = my_cJSON_Array;
    value = skip(value + 1);
    if (*value == ']') {
        return value + 1;	/* empty array. */
    }

    item->child = child = my_cJSON_New_Item();
    if (!item->child) {
        return 0;		 /* memory fail */
    }

    value = skip(parse_value(child, skip(value)));	/* skip any spacing, get the value. */
    if (!value) {
        return 0;
    }

    while (*value == ',') {
        my_cJSON *new_item;
        if (!(new_item = my_cJSON_New_Item())) {
            return 0; 	/* memory fail */
        }
        child->next = new_item;
        new_item->prev = child;
        child = new_item;
        value = skip(parse_value(child, skip(value + 1)));
        if (!value) {
            return 0;	/* memory fail */
        }
    }

    if (*value == ']') {
        return value + 1;	/* end of array */
    }
    ep = value;
    return 0;	/* malformed. */
}

/* Render an array to text */
static char *print_array(my_cJSON *item, int depth, int fmt)
{
    char **entries;
    char *out = 0, *ptr, *ret;
    int len = 5;
    my_cJSON *child = item->child;
    int numentries = 0, i = 0, fail = 0;

    /* How many entries in the array? */
    while (child) {
        numentries++;
        child = child->next;
    }

    /* Explicitly handle numentries==0 */
    if (!numentries) {
        out = (char *)my_cJSON_malloc(3);
        if (out) {
            strcpy(out, "[]");
        }
        return out;
    }
    /* Allocate an array to hold the values for each */
    entries = (char **)my_cJSON_malloc(numentries * sizeof(char *));
    if (!entries) {
        return 0;
    }
    memset(entries, 0, numentries * sizeof(char *));
    /* Retrieve all the results: */
    child = item->child;
    while (child && !fail) {
        ret = print_value(child, depth + 1, fmt);
        entries[i++] = ret;
        if (ret) {
            len += strlen(ret) + 2 + (fmt ? 1 : 0);
        } else {
            fail = 1;
        }
        child = child->next;
    }

    /* If we didn't fail, try to malloc the output string */
    if (!fail) {
        out = (char *)my_cJSON_malloc(len);
    }
    /* If that fails, we fail. */
    if (!out) {
        fail = 1;
    }

    /* Handle failure. */
    if (fail) {
        for (i = 0; i < numentries; i++) {
            if (entries[i]) {
                my_cJSON_free(entries[i]);
            }
        }
        my_cJSON_free(entries);
        return 0;
    }

    /* Compose the output array. */
    *out = '[';
    ptr = out + 1;
    *ptr = 0;
    for (i = 0; i < numentries; i++) {
        strcpy(ptr, entries[i]);
        ptr += strlen(entries[i]);
        if (i != numentries - 1) {
            *ptr++ = ',';
            if (fmt) {
                *ptr++ = ' ';
            }
            *ptr = 0;
        }
        my_cJSON_free(entries[i]);
    }
    my_cJSON_free(entries);
    *ptr++ = ']';
    *ptr++ = 0;
    return out;
}

/* Build an object from the text. */
static const char *parse_object(my_cJSON *item, const char *value)
{
    my_cJSON *child;
    if (*value != '{') {
        ep = value;
        return 0;
    }	/* not an object! */

    item->type = my_cJSON_Object;
    value = skip(value + 1);
    if (*value == '}') {
        return value + 1;	/* empty array. */
    }

    item->child = child = my_cJSON_New_Item();
    if (!item->child) {
        return 0;
    }

    value = skip(parse_string(child, skip(value)));
    if (!value) {
        return 0;
    }
    child->string = child->valuestring;
    child->valuestring = 0;

    if (*value != ':') {
        ep = value;
        return 0;
    }	/* fail! */
    value = skip(parse_value(child, skip(value + 1)));	/* skip any spacing, get the value. */
    if (!value) {
        return 0;
    }

    while (*value == ',') {
        my_cJSON *new_item;
        if (!(new_item = my_cJSON_New_Item())) {
            return 0; /* memory fail */
        }
        child->next = new_item;
        new_item->prev = child;
        child = new_item;
        value = skip(parse_string(child, skip(value + 1)));
        if (!value) {
            return 0;
        }
        child->string = child->valuestring;
        child->valuestring = 0;
        if (*value != ':') {
            ep = value;
            return 0;
        }	/* fail! */
        value = skip(parse_value(child, skip(value + 1)));	/* skip any spacing, get the value. */
        if (!value) {
            return 0;
        }
    }

    if (*value == '}') {
        return value + 1;	/* end of array */
    }
    ep = value;
    return 0;	/* malformed. */
}

/* Render an object to text. */
static char *print_object(my_cJSON *item, int depth, int fmt)
{
    char **entries = 0, **names = 0;
    char *out = 0, *ptr, *ret, *str;
    int len = 7, i = 0, j;
    my_cJSON *child = item->child;
    int numentries = 0, fail = 0;
    /* Count the number of entries. */
    while (child) {
        numentries++;
        child = child->next;
    }
    /* Explicitly handle empty object case */
    if (!numentries) {
        out = (char *)my_cJSON_malloc(fmt ? depth + 4 : 3);
        if (!out) {
            return 0;
        }
        ptr = out;
        *ptr++ = '{';
        if (fmt) {
            *ptr++ = '\n';
            for (i = 0; i < depth - 1; i++) {
                *ptr++ = '\t';
            }
        }
        *ptr++ = '}';
        *ptr++ = 0;
        return out;
    }
    /* Allocate space for the names and the objects */
    entries = (char **)my_cJSON_malloc(numentries * sizeof(char *));
    if (!entries) {
        return 0;
    }
    names = (char **)my_cJSON_malloc(numentries * sizeof(char *));
    if (!names) {
        my_cJSON_free(entries);
        return 0;
    }
    memset(entries, 0, sizeof(char *)*numentries);
    memset(names, 0, sizeof(char *)*numentries);

    /* Collect all the results into our arrays: */
    child = item->child;
    depth++;
    if (fmt) {
        len += depth;
    }
    while (child) {
        str = print_string_ptr(child->string);
        names[i] = str;
        ret = print_value(child, depth, fmt);
        entries[i++] = ret;
        if (str && ret) {
            len += strlen(ret) + strlen(str) + 2 + (fmt ? 2 + depth : 0);
        } else {
            fail = 1;
        }
        child = child->next;
    }

    /* Try to allocate the output string */
    if (!fail) {
        out = (char *)my_cJSON_malloc(len);
    }
    if (!out) {
        fail = 1;
    }

    /* Handle failure */
    if (fail) {
        for (i = 0; i < numentries; i++) {
            if (names[i]) {
                my_cJSON_free(names[i]);
            }
            if (entries[i]) {
                my_cJSON_free(entries[i]);
            }
        }
        my_cJSON_free(names);
        my_cJSON_free(entries);
        return 0;
    }

    /* Compose the output: */
    *out = '{';
    ptr = out + 1;
    if (fmt) {
        *ptr++ = '\n';
    }
    *ptr = 0;
    for (i = 0; i < numentries; i++) {
        if (fmt) {
            for (j = 0; j < depth; j++) {
                *ptr++ = '\t';
            }
        }
        strcpy(ptr, names[i]);
        ptr += strlen(names[i]);
        *ptr++ = ':';
        if (fmt) {
            *ptr++ = '\t';
        }
        strcpy(ptr, entries[i]);
        ptr += strlen(entries[i]);
        if (i != numentries - 1) {
            *ptr++ = ',';
        }
        if (fmt) {
            *ptr++ = '\n';
        }
        *ptr = 0;
        my_cJSON_free(names[i]);
        my_cJSON_free(entries[i]);
    }

    my_cJSON_free(names);
    my_cJSON_free(entries);
    if (fmt) {
        for (i = 0; i < depth - 1; i++) {
            *ptr++ = '\t';
        }
    }
    *ptr++ = '}';
    *ptr++ = 0;
    return out;
}

/* Get Array size/item / object item. */
int    my_cJSON_GetArraySize(my_cJSON *array)
{
    my_cJSON *c = array->child;
    int i = 0;
    while (c) {
        i++;
        c = c->next;
    }
    return i;
}
my_cJSON *my_cJSON_GetArrayItem(my_cJSON *array, int item)
{
    my_cJSON *c = array->child;
    while (c && item > 0) {
        item--;
        c = c->next;
    }
    return c;
}
my_cJSON *my_cJSON_GetObjectItem(my_cJSON *object, const char *string)
{
    my_cJSON *c = object->child;
    while (c && my_cJSON_strcasecmp(c->string, string)) {
        c = c->next;
    }
    return c;
}

/* Utility for array list handling. */
static void suffix_object(my_cJSON *prev, my_cJSON *item)
{
    prev->next = item;
    item->prev = prev;
}
/* Utility for handling references. */
static my_cJSON *create_reference(my_cJSON *item)
{
    my_cJSON *ref = my_cJSON_New_Item();
    if (!ref) {
        return 0;
    }
    memcpy(ref, item, sizeof(my_cJSON));
    ref->string = 0;
    ref->type |= my_cJSON_IsReference;
    ref->next = ref->prev = 0;
    return ref;
}

/* Add item to array/object. */
void my_cJSON_AddItemToArray(my_cJSON *array, my_cJSON *item)
{
    my_cJSON *c = array->child;
    if (!item) {
        return;
    }
    if (!c) {
        array->child = item;
    } else {
        while (c && c->next) {
            c = c->next;
        }
        suffix_object(c, item);
    }
}

void my_cJSON_AddItemToObject(my_cJSON *object, const char *string, my_cJSON *item)
{
    if (!item) {
        return;
    }
    if (item->string) {
        my_cJSON_free(item->string);
    }
    item->string = my_cJSON_strdup(string);
    my_cJSON_AddItemToArray(object, item);
}

void my_cJSON_AddItemReferenceToArray(my_cJSON *array, my_cJSON *item)
{
    my_cJSON_AddItemToArray(array, create_reference(item));
}
void my_cJSON_AddItemReferenceToObject(my_cJSON *object, const char *string, my_cJSON *item)
{
    my_cJSON_AddItemToObject(object, string, create_reference(item));
}

my_cJSON *my_cJSON_DetachItemFromArray(my_cJSON *array, int which)
{
    my_cJSON *c = array->child;
    while (c && which > 0) {
        c = c->next;
        which--;
    }
    if (!c) {
        return 0;
    }
    if (c->prev) {
        c->prev->next = c->next;
    }
    if (c->next) {
        c->next->prev = c->prev;
    }
    if (c == array->child) {
        array->child = c->next;
    }
    c->prev = 0;
    c->next = 0;
    return c;
}
void   my_cJSON_DeleteItemFromArray(my_cJSON *array, int which)
{
    my_cJSON_Delete(my_cJSON_DetachItemFromArray(array, which));
}
my_cJSON *my_cJSON_DetachItemFromObject(my_cJSON *object, const char *string)
{
    int i = 0;
    my_cJSON *c = object->child;
    while (c && my_cJSON_strcasecmp(c->string, string)) {
        i++;
        c = c->next;
    }
    if (c) {
        return my_cJSON_DetachItemFromArray(object, i);
    }
    return 0;
}
void   my_cJSON_DeleteItemFromObject(my_cJSON *object, const char *string)
{
    my_cJSON_Delete(my_cJSON_DetachItemFromObject(object, string));
}

/* Replace array/object items with new ones. */
void   my_cJSON_ReplaceItemInArray(my_cJSON *array, int which, my_cJSON *newitem)
{
    my_cJSON *c = array->child;
    while (c && which > 0) {
        c = c->next;
        which--;
    }
    if (!c) {
        return;
    }

    newitem->next = c->next;
    newitem->prev = c->prev;
    if (newitem->next) {
        newitem->next->prev = newitem;
    }
    if (c == array->child) {
        array->child = newitem;
    } else {
        newitem->prev->next = newitem;
    }
    c->next = 0;
    c->prev = 0;
    my_cJSON_Delete(c);
}
void   my_cJSON_ReplaceItemInObject(my_cJSON *object, const char *string, my_cJSON *newitem)
{
    int i = 0;
    my_cJSON *c = object->child;
    while (c && my_cJSON_strcasecmp(c->string, string)) {
        i++;
        c = c->next;
    }
    if (c) {
        newitem->string = my_cJSON_strdup(string);
        my_cJSON_ReplaceItemInArray(object, i, newitem);
    }
}

/* Create basic types: */
my_cJSON *my_cJSON_CreateNull(void)
{
    my_cJSON *item = my_cJSON_New_Item();
    if (item) {
        item->type = my_cJSON_NULL;
    }
    return item;
}
my_cJSON *my_cJSON_CreateTrue(void)
{
    my_cJSON *item = my_cJSON_New_Item();
    if (item) {
        item->type = my_cJSON_True;
    }
    return item;
}
my_cJSON *my_cJSON_CreateFalse(void)
{
    my_cJSON *item = my_cJSON_New_Item();
    if (item) {
        item->type = my_cJSON_False;
    }
    return item;
}
my_cJSON *my_cJSON_CreateBool(int b)
{
    my_cJSON *item = my_cJSON_New_Item();
    if (item) {
        item->type = b ? my_cJSON_True : my_cJSON_False;
    }
    return item;
}
my_cJSON *my_cJSON_CreateNumber(double num)
{
    my_cJSON *item = my_cJSON_New_Item();
    if (item) {
        item->type = my_cJSON_Number;
        item->valuedouble = num;
        item->valueint = (int)num;
    }
    return item;
}
my_cJSON *my_cJSON_CreateString(const char *string)
{
    my_cJSON *item = my_cJSON_New_Item();
    if (item) {
        item->type = my_cJSON_String;
        item->valuestring = my_cJSON_strdup(string);
    }
    return item;
}
my_cJSON *my_cJSON_CreateArray(void)
{
    my_cJSON *item = my_cJSON_New_Item();
    if (item) {
        item->type = my_cJSON_Array;
    }
    return item;
}
my_cJSON *my_cJSON_CreateObject(void)
{
    my_cJSON *item = my_cJSON_New_Item();
    if (item) {
        item->type = my_cJSON_Object;
    }
    return item;
}

/* Create Arrays: */
my_cJSON *my_cJSON_CreateIntArray(const int *numbers, int count)
{
    int i;
    my_cJSON *n = 0, *p = 0, *a = my_cJSON_CreateArray();
    for (i = 0; a && i < count; i++) {
        n = my_cJSON_CreateNumber(numbers[i]);
        if (!i) {
            a->child = n;
        } else {
            suffix_object(p, n);
        }
        p = n;
    }
    return a;
}
my_cJSON *my_cJSON_CreateFloatArray(const float *numbers, int count)
{
    int i;
    my_cJSON *n = 0, *p = 0, *a = my_cJSON_CreateArray();
    for (i = 0; a && i < count; i++) {
        n = my_cJSON_CreateNumber(numbers[i]);
        if (!i) {
            a->child = n;
        } else {
            suffix_object(p, n);
        }
        p = n;
    }
    return a;
}
my_cJSON *my_cJSON_CreateDoubleArray(const double *numbers, int count)
{
    int i;
    my_cJSON *n = 0, *p = 0, *a = my_cJSON_CreateArray();
    for (i = 0; a && i < count; i++) {
        n = my_cJSON_CreateNumber(numbers[i]);
        if (!i) {
            a->child = n;
        } else {
            suffix_object(p, n);
        }
        p = n;
    }
    return a;
}
my_cJSON *my_cJSON_CreateStringArray(const char **strings, int count)
{
    int i;
    my_cJSON *n = 0, *p = 0, *a = my_cJSON_CreateArray();
    for (i = 0; a && i < count; i++) {
        n = my_cJSON_CreateString(strings[i]);
        if (!i) {
            a->child = n;
        } else {
            suffix_object(p, n);
        }
        p = n;
    }
    return a;
}

/* Duplication */
my_cJSON *my_cJSON_Duplicate(my_cJSON *item, int recurse)
{
    my_cJSON *newitem, *cptr, *nptr = 0, *newchild;
    /* Bail on bad ptr */
    if (!item) {
        return 0;
    }
    /* Create new item */
    newitem = my_cJSON_New_Item();
    if (!newitem) {
        return 0;
    }
    /* Copy over all vars */
    newitem->type = item->type & (~my_cJSON_IsReference);
    newitem->valueint = item->valueint;
    newitem->valuedouble = item->valuedouble;
    if (item->valuestring) {
        newitem->valuestring = my_cJSON_strdup(item->valuestring);
        if (!newitem->valuestring) {
            my_cJSON_Delete(newitem);
            return 0;
        }
    }
    if (item->string) {
        newitem->string = my_cJSON_strdup(item->string);
        if (!newitem->string) {
            my_cJSON_Delete(newitem);
            return 0;
        }
    }
    /* If non-recursive, then we're done! */
    if (!recurse) {
        return newitem;
    }
    /* Walk the ->next chain for the child. */
    cptr = item->child;
    while (cptr) {
        newchild = my_cJSON_Duplicate(cptr, 1);		/* Duplicate (with recurse) each item in the ->next chain */
        if (!newchild) {
            my_cJSON_Delete(newitem);
            return 0;
        }
        if (nptr) {
            nptr->next = newchild;
            newchild->prev = nptr;
            nptr = newchild;
        }	/* If newitem->child already set, then crosswire ->prev and ->next and move on */
        else {
            newitem->child = newchild;
            nptr = newchild;
        }					/* Set newitem->child and move to it */
        cptr = cptr->next;
    }
    return newitem;
}

void my_cJSON_Minify(char *json)
{
    char *into = json;
    while (*json) {
        if (*json == ' ') {
            json++;
        } else if (*json == '\t') {
            json++;	// Whitespace characters.
        } else if (*json == '\r') {
            json++;
        } else if (*json == '\n') {
            json++;
        } else if (*json == '/' && json[1] == '/') {
            while (*json && *json != '\n') {
                json++;	// double-slash comments, to end of line.
            }
        } else if (*json == '/' && json[1] == '*') {
            while (*json && !(*json == '*' && json[1] == '/')) {
                json++;
            }
            json += 2;
        }	// multiline comments.
        else if (*json == '\"') {
            *into++ = *json++;
            while (*json && *json != '\"') {
                if (*json == '\\') {
                    *into++ = *json++;
                }
                *into++ = *json++;
            }
            *into++ = *json++;
        } // string literals, which are \" sensitive.
        else {
            *into++ = *json++;			// All other characters.
        }
    }
    *into = 0;	// and null-terminate.
}


