
#include <syscall.h>
#include <linux/errno.h>


int fcntl(int fd, int cmd, long arg)
{
	if (fd<0 || cmd<0 || arg<0)
                return -EINVAL;
        return syscall(__NR_fcntl, (long)fd, (long)cmd, (long)arg);
}

