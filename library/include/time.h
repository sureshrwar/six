#ifndef _TIME_H
#define _TIME_H

#include <linux/types.h>
#include <linux/time.h>
#include <linux/times.h>

time_t time(time_t *tloc);
struct tm *gmtime(const time_t *timer);
struct tm *localtime(const time_t *timer);
char *asctime(const struct tm *timeptr);
char *ctime(const time_t *timer);
time_t mktime(struct tm *timep);
clock_t clock(void);

#endif
