
#include <syscall.h>
#include <linux/errno.h>


int uselib(char *name)
{
	if (!name)
	        return -EINVAL;
        return syscall(__NR_uselib, (long)name, 0, 0);
}

