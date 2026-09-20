
#include <syscall.h>
#include <linux/errno.h>
#include <errno.h>

int socketcall(int call, unsigned long *args)
{
	int ret;
	if (!args) {
		errno = EINVAL;
		return -1;
	}
	ret = syscall(__NR_socketcall, (long)call, (long)args, 0);
	if (ret < 0) {
		errno = -ret;
		return -1;
	}
	return ret;
}

