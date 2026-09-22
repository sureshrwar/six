
#include <syscall.h>
#include <linux/errno.h>


int chmod(char *name, int mode)
{
	if (!name || mode<0)
                return __syscall_error(EINVAL);
        return __syscall_return(syscall(__NR_chmod, (long)name, (long)mode, 0));
}

