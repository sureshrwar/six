
#include <syscall.h>
#include <linux/errno.h>
#include <errno.h>

int read(int fd, char *str, int len)
{
	int ret;
	if (fd < 0 || !str || len < 0) {
		errno = EINVAL;
		return -1;
	}
	ret = syscall(__NR_read, (long)fd, (long)str, (long)len);
	if (ret < 0) {
		errno = -ret;
		return -1;
	}
	return ret;
}
