
#ifndef _LINUX_TIME_H
#define _LINUX_TIME_H

#if (SIX)
#define ITIMER_REAL	0
#define ITIMER_VIRTUAL	1
#define ITIMER_PROF     2
#endif

#ifndef _STRUCT_TIMESPEC
#define _STRUCT_TIMESPEC
struct timespec {
        long    tv_sec;         /* seconds */
        long    tv_nsec;        /* nanoseconds */
};
#endif /* _STRUCT_TIMESPEC */


struct timeval {
        int     tv_sec;         
        int     tv_usec;        
};

struct timezone {
        int     tz_minuteswest; /* minutes west of Greenwich */
        int     tz_dsttime;     /* type of dst correction */
};


struct  itimerval {
        struct  timeval it_interval;    
        struct  timeval it_value;       
};

#define FD_SETSIZE              __FD_SETSIZE
#define FD_SET(fd,fdsetp)       __FD_SET(fd,fdsetp)
#define FD_CLR(fd,fdsetp)       __FD_CLR(fd,fdsetp)
#define FD_ISSET(fd,fdsetp)     __FD_ISSET(fd,fdsetp)
#define FD_ZERO(fdsetp)         __FD_ZERO(fdsetp)




#endif
