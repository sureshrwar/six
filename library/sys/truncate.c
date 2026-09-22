
#include <syscall.h>
#include <linux/errno.h>


int truncate(char *path, unsigned long len)
{
	if (!path || len<0)
                return __syscall_error(EINVAL);
        return __syscall_return(syscall(__NR_truncate, (long)path, (long)len, 0));
}

