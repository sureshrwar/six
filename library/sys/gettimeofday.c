
#include <syscall.h>
#include <linux/errno.h>
#include <linux/time.h>


int gettimeofday(struct timeval *tv, struct timezone *tz)
{
	if (!tv || !tz)
                return -EINVAL;
        return syscall(__NR_gettimeofday, (long)tv, (long)tz, 0);
}

