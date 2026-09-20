
#include <syscall.h>
#include <linux/errno.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>

int open(const char *name, int flags, ...)
{
	int ret;
	int mode = 0;
	if (!name || flags < 0) {
		errno = EINVAL;
		return -1;
	}
	if (flags & O_CREAT) {
		va_list ap;
		va_start(ap, flags);
		mode = va_arg(ap, int);
		va_end(ap);
	}
	ret = syscall(__NR_open, (long)name, (long)flags, (long)mode);
	if (ret < 0) {
		errno = -ret;
		return -1;
	}
	return ret;	
}
