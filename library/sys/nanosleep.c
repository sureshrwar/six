
#include <syscall.h>
#include <linux/errno.h>
#include <linux/time.h>


int nanosleep(struct timespec *rqtp, struct timespec *rmtp)
{
        if (!rqtp)
                return __syscall_error(EINVAL);
        return __syscall_return(syscall(__NR_nanosleep, (long)rqtp, (long)rmtp, 0));
}

