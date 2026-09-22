
#include <syscall.h>
#include <linux/errno.h>
#include <linux/time.h>


int setitimer(int which, struct itimerval *val, struct itimerval *oval)
{
        return __syscall_return(syscall(__NR_setitimer, (long)which, (long)val, (long)oval));
}

