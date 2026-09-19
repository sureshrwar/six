
#include <syscall.h>
#include <linux/errno.h>
#include <linux/time.h>


int getitimer(int which, struct itimerval *val)
{
	if (!val)
		return -EINVAL;
        return syscall(__NR_getitimer, (long)which, (long)val, 0);
}

