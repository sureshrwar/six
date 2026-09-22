
#include <syscall.h>
#include <linux/errno.h>
#include <asm/statfs.h>


int statfs(char *path, struct statfs *s)
{
	if (!path || !s)
                return __syscall_error(EINVAL);
        return __syscall_return(syscall(__NR_statfs, (long)path, (long)s, 0));
}

