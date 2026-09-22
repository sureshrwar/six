
#include <syscall.h>
#include <linux/errno.h>
#include <stat.h>


int stat(const char *name, struct stat *s)
{
	if (!name || !s)
        	return __syscall_error(EINVAL);
        return __syscall_return(syscall(__NR_stat, (long)name, (long)s, 0));
}

