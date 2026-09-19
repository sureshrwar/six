
#include <syscall.h>
#include <linux/errno.h>


int unlink(char *name)
{
	if (!name)
                return -EINVAL;
        return syscall(__NR_unlink, (long)name, 0, 0);
}

