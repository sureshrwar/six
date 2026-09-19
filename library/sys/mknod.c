
#include <syscall.h>
#include <linux/errno.h>


int mknod(char *name, int mode, long dev)
{
	if (!name || mode<0 || dev<0)
                return -EINVAL;
        return syscall(__NR_mknod, (long)name, (long)mode, dev);
}


