#include <linux/types.h>
#include <linux/time.h>
#include <linux/times.h>
#include <loc_time.h>

struct tm *
localtime(const time_t *timer) 
{       
        struct tm *timep;
        unsigned dst;
        
        _tzset();
        timep = gmtime(timer);                  /* tm->tm_isdst == 0 */
        timep->tm_min -= _timezone / 60;
        timep->tm_sec -= _timezone % 60;
        mktime(timep); 
        
        dst = _dstget(timep);
        if (dst) {
                timep->tm_min += dst / 60;
                timep->tm_sec += dst % 60;
                mktime(timep);
        }
        return timep;
}               

