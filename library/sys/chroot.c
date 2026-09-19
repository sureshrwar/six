
#include <syscall.h>
#include <linux/errno.h>


int chroot(char *name)
{
	if (!name)
                return -EINVAL;
        return syscall(__NR_chroot, (long)name, 0, 0);
}

