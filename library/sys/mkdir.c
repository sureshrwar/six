
#include <syscall.h>
#include <linux/errno.h>


int mkdir(char *name, int mode)
{
	if (!name || mode<0)
                return -EINVAL;
        return syscall(__NR_mkdir, (long)name, (long)mode, 0);
}


