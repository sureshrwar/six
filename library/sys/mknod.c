
#include <syscall.h>
#include <linux/errno.h>


int mknod(char *name, int mode, long dev)
{
	if (!name || mode<0 || dev<0)
                return __syscall_error(EINVAL);
        return __syscall_return(syscall(__NR_mknod, (long)name, (long)mode, dev));
}


