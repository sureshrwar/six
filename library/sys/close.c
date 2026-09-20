
#include <syscall.h>
#include <linux/errno.h>
#include <errno.h>

int close(int fd)
{
	int ret;
	if (fd < 0) {
		errno = EBADF;
		return -1;
	}
	ret = syscall(__NR_close, (long)fd, 0, 0);
	if (ret < 0) {
		errno = -ret;
		return -1;
	}
	return ret;
}
