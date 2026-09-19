
#include <syscall.h>
#include <linux/errno.h>
#include <linux/time.h>


int nanosleep(struct timespec *rqtp, struct timespec *rmtp)
{
        if (!rqtp)
                return -EINVAL;
        return syscall(__NR_nanosleep, (long)rqtp, (long)rmtp, 0);
}

