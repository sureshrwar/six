
#include <syscall.h>
#include <linux/errno.h>


int readlink(char *name, char *buf, int size)
{
	if (!name || !buf || size<0)
		return -EINVAL;
        return syscall(__NR_readlink, (long)name, (long)buf, (long)size);
}

