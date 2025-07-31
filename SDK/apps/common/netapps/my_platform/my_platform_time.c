#include "my_platform_time.h"

void my_get_sys_time(struct sys_time *time)//获取时间
{
    void *fd = dev_open("rtc", NULL);
    if (!fd) {
        return ;
    }
    dev_ioctl(fd, IOCTL_GET_SYS_TIME, (u32)time);
    dev_close(fd);
}

/* Number of days per month */
static const u8  days_per_month[MONTH_PER_YEAR] = {
    /*
    1   2   3   4   5   6   7   8   9   10  11  12
    */
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static bool get_is_leap_year(u16 year)
{
    if (((year % 4) == 0) && ((year % 100) != 0)) {
        return 1;
    } else if ((year % 400) == 0) {
        return 1;
    }

    return 0;
}

static u8 get_day_of_mon(u8 month, u16 year)
{
    if ((month == 0) || (month > 12)) {
        return days_per_month[1] + get_is_leap_year(year);
    }

    if (month != 2) {
        return days_per_month[month - 1];
    } else {
        return days_per_month[1] + get_is_leap_year(year);
    }
}

void my_utc_sec_2_mytime(u32 utc_sec, struct sys_time *mytime)
{
    int32_t sec, day;
    u16	y;
    u8 m;
    u16	d;
    sec = utc_sec % SEC_PER_DAY;
    mytime->hour = sec / SEC_PER_HOUR;
    sec %= SEC_PER_HOUR;
    mytime->min = sec / SEC_PER_MIN;
    mytime->sec = sec % SEC_PER_MIN;
    day = utc_sec / SEC_PER_DAY;
    for (y = UTC_BASE_YEAR; day > 0; y++) {
        d = (DAY_PER_YEAR + get_is_leap_year(y));
        if (day >= d) {
            day -= d;
        } else {
            break;
        }
    }
    mytime->year = y;
    for (m = 1; m < MONTH_PER_YEAR; m++) {
        d = get_day_of_mon(m, y);
        if (day >= d) {
            day -= d;
        } else {
            break;
        }
    }
    mytime->month = m;
    mytime->day = (u8)(day + 1);
}

u32 my_mytime_2_utc_sec(struct sys_time *mytime)
{
    u16	i;
    u32 no_of_days = 0;
    u32 utc_time;
    u8  dst = 1;
    if (mytime->year < UTC_BASE_YEAR) {
        return 0;
    }
    for (i = UTC_BASE_YEAR; i < mytime->year; i++) {
        no_of_days += (DAY_PER_YEAR + get_is_leap_year(i));
    }
    for (i = 1; i < mytime->month; i++) {
        no_of_days += get_day_of_mon(i, mytime->year);
    }
    no_of_days += (mytime->day - 1);
    utc_time = (unsigned int) no_of_days * SEC_PER_DAY + (unsigned int)(mytime->hour * SEC_PER_HOUR +
               mytime->min * SEC_PER_MIN + mytime->sec);

    return utc_time;
}


void my_get_timestamp(const struct sys_time *t, char *buffer, size_t size)
{
    printf("year %d month %d day %d hour %d min  %d sec %d \n", t->year, t->month, t->day, t->hour, t->min, t->sec);
    snprintf(buffer, size, "%04u-%02u-%02uT%02u:%02u:%02uZ",
             t->year, t->month, t->day, t->hour, t->min, t->sec);
}



#define WDAY_COUNT 7
#define MON_COUNT 12
static const char wday_name[][4] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", ""
};

static const char mon_name[][4] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec", ""
};



size_t my_strftime_2(char *ptr, size_t maxsize, const char *format, const struct tm *timeptr)
{
    if ((timeptr == NULL) || (ptr == NULL)) {
        return -1;
    }

    if (timeptr->tm_mday < 10) {
        return snprintf(ptr, maxsize, "%s, 0%d %s %d %02d:%02d:%02d GMT",
                        wday_name[timeptr->tm_wday],
                        timeptr->tm_mday,
                        mon_name[timeptr->tm_mon],
                        timeptr->tm_year + 1900,
                        timeptr->tm_hour,
                        timeptr->tm_min,
                        timeptr->tm_sec);
    } else {
        return snprintf(ptr, maxsize, "%s, %d %s %d %02d:%02d:%02d GMT",
                        wday_name[timeptr->tm_wday],
                        timeptr->tm_mday,
                        mon_name[timeptr->tm_mon],
                        timeptr->tm_year + 1900,
                        timeptr->tm_hour,
                        timeptr->tm_min,
                        timeptr->tm_sec);
    }
}






















#if 0
void my_api_get_http_time()
{
    //记得开rtc
    int ntp = ntp_client_get_time("s2c.time.edu.cn");
    if (ntp == -1) {
        MY_LOG_I("get ntp error!!\n");
        return;
    }
    struct sys_time curtime;
    my_get_sys_time(&curtime);
    MY_LOG_I("Current Time Parameters:\n");
    MY_LOG_I("Year:  %d\n", curtime.year);
    MY_LOG_I("Month: %d\n", curtime.month);
    MY_LOG_I("Day:   %d\n", curtime.day);
    MY_LOG_I("Hour:  %d\n", curtime.hour);
    MY_LOG_I("Minute:%d\n", curtime.min);
    MY_LOG_I("Second:%d\n", curtime.sec);
    u32 utc = my_mytime_2_utc_sec(&curtime);
    MY_LOG_I(">>>>>>utc %d \n", utc);
    memset(&curtime, 0, sizeof(struct sys_time));
    my_utc_sec_2_mytime(utc, &curtime);
    MY_LOG_I("Current Time Parameters:\n");
    MY_LOG_I("Year:  %d\n", curtime.year);
    MY_LOG_I("Month: %d\n", curtime.month);
    MY_LOG_I("Day:   %d\n", curtime.day);
    MY_LOG_I("Hour:  %d\n", curtime.hour);
    MY_LOG_I("Minute:%d\n", curtime.min);
    MY_LOG_I("Second:%d\n", curtime.sec);
}
#endif

#define CPU_RAND()	(JL_RAND->R64L)
extern unsigned int random32(int type);

void get_random_bytes(unsigned char *buf, int nbytes)
{
    while (nbytes--) {
        *buf = random32(0);
        ++buf;
    }
}

int rand_val(void *p_rng, unsigned char *output, unsigned int output_len)
{
    get_random_bytes(output, output_len);
    return 0;
}



