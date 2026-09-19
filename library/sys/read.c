
#include <syscall.h>
#include <linux/errno.h>


int read(int fd, char *str, int len)
{
	if (fd<0 || !str || len<0)
                return -EINVAL;
	return syscall(__NR_read, (long)fd, (long)str, (long)len);
}
