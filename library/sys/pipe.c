
#include <syscall.h>
#include <linux/errno.h>


int pipe(int *fd)
{
	if (fd<0)
                return -EINVAL;
        return syscall(__NR_pipe, (long)fd, 0, 0);
}


