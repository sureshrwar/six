
#include <syscall.h>
#include <linux/errno.h>


int stime(int *t)
{
	if (!t)
                return -EINVAL;
        return syscall(__NR_stime, (long)t, 0, 0);
}


