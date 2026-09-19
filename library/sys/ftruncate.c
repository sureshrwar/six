
#include <syscall.h>
#include <linux/errno.h>


int ftruncate(int fd, unsigned long len)
{
	if (fd<0)
                return -EINVAL;
        return syscall(__NR_ftruncate, (long)fd, (long)len, 0);
}

