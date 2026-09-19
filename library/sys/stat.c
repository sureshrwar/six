
#include <syscall.h>
#include <linux/errno.h>
#include <stat.h>


int stat(char *name, struct stat *s)
{
	if (!name || !s)
        	return -EINVAL;
        return syscall(__NR_stat, (long)name, (long)s, 0);
}

