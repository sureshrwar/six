
#include <syscall.h>
#include <linux/errno.h>

int creat(char *pathname, int mode)
{
	if (!pathname || mode<0)
                return -EINVAL;
        return syscall(__NR_creat, (long)pathname, (long)mode, 0);
}

