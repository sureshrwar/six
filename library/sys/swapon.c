
#include <syscall.h>
#include <linux/errno.h>


int swapon(char *name, int flags)
{
	if (!name)
                return -EINVAL;
        return syscall(__NR_swapon, (long)name, (long)flags, 0);
}

