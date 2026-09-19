
#include <syscall.h>
#include <linux/errno.h>
#include <asm/statfs.h>


int fstatfs(int fd, struct statfs *s)
{
	if (fd<0 || !s)
                return -EINVAL;
        return syscall(__NR_fstatfs, (long)fd, (long)s, 0);
}

