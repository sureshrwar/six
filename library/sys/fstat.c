
#include <syscall.h>
#include <linux/errno.h>
#include <stat.h>


int fstat(int fd, struct stat *s)
{
	if (fd<0 || !s)
                return -EINVAL;
	return syscall(__NR_fstat, (long)fd, (long)s, 0);
}
