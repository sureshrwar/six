
#include <syscall.h>
#include <linux/errno.h>


int symlink(char *old, char *new)
{
	if (!old || !new)
                return -EINVAL;
        return syscall(__NR_symlink, (long)old, (long)new, 0);
}

