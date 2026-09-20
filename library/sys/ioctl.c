
#include <syscall.h>
#include <linux/errno.h>
#include <errno.h>

int ioctl(int fd, long cmd, long arg)
{
	int ret;
	if (fd < 0 || cmd < 0 || arg < 0) {
		errno = EINVAL;
		return -1;
	}
	ret = syscall(__NR_ioctl, (long)fd, (long)cmd, (long)arg);
	if (ret < 0) {
		errno = -ret;
		return -1;
	}
	return ret;
}
