
#include <syscall.h>
#include <linux/errno.h>


int umount(char *name)
{
	if (!name)
                return -EINVAL;
        return syscall(__NR_umount, (long)name, 0, 0);
}

