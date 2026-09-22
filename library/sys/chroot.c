
#include <syscall.h>
#include <linux/errno.h>


int chroot(char *name)
{
	if (!name)
                return __syscall_error(EINVAL);
        return __syscall_return(syscall(__NR_chroot, (long)name, 0, 0));
}

