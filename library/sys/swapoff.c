
#include <syscall.h>
#include <linux/errno.h>


int swapoff(char *name)
{
	if (!name)
                return -EINVAL;
        return syscall(__NR_swapoff, (long)name, 0, 0);
}

