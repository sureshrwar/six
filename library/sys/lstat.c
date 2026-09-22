
#include <syscall.h>
#include <linux/errno.h>
#include <linux/stat.h>


int lstat(char *name, struct old_stat *s)
{
	if (!name || !s)
        	return __syscall_error(EINVAL);
        return __syscall_return(syscall(__NR_lstat, (long)name, (long)s, 0));
}

