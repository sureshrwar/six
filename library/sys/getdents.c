
#include <syscall.h>
#include <linux/errno.h>


int getdents(int fd, void *buf, int count)
{
	if (fd<0 || !buf || count<0)
                return __syscall_error(EINVAL);
	return __syscall_return(syscall(__NR_getdents, (long)fd, (long)buf, (long)count));
}
