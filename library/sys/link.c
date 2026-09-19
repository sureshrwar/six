
#include <syscall.h>
#include <linux/errno.h>


int link(char *old, char *new)
{
	if (!old || !new)
                return -EINVAL;
        return syscall(__NR_link, (long)old, (long)new, 0);
}

