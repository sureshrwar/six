
#include <syscall.h>
#include <linux/errno.h>
#include <errno.h>


int open(char *name, int flags)
{
	int ret;
	if (!name || flags<0)
                return -EINVAL;
	ret = syscall(__NR_open, (long)name, (long)flags, 0);
	if (ret == -ENOENT)
		errno = ENOENT;
	return ret;	
}
