
#include <syscall.h>
#include <linux/errno.h>


int truncate(char *path, unsigned long len)
{
	if (!path || len<0)
                return -EINVAL;
        return syscall(__NR_truncate, (long)path, (long)len, 0);
}

