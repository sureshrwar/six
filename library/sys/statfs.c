
#include <syscall.h>
#include <linux/errno.h>
#include <asm/statfs.h>


int statfs(char *path, struct statfs *s)
{
	if (!path || !s)
                return -EINVAL;
        return syscall(__NR_statfs, (long)path, (long)s, 0);
}

