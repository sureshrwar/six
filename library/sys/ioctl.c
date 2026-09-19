
#include <syscall.h>
#include <linux/errno.h>


int ioctl(int fd, long cmd, long arg)
{
	if (fd<0 || cmd<0 || arg<0)
                return -EINVAL;
	return syscall(__NR_ioctl, (long)fd, (long)cmd, (long)arg);
}
