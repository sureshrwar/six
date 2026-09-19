
#include <syscall.h>
#include <errno.h>

int access(char *filename, int mode)
{
	int ret;
	if (!filename || mode < 0)
		return -EINVAL;
	ret = syscall(__NR_access, (long)filename, (long)mode, 0);
	if (ret < 0)
	{
		errno = -ret;
		return -1;
	}
	return errno = 0;
}
