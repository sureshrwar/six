
#include <syscall.h>
#include <linux/errno.h>


int rmdir(char *name)
{
	if (!name)
                return -EINVAL;
        return syscall(__NR_rmdir, (long)name, 0, 0);
}


