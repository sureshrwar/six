
#include <syscall.h>
#include <linux/errno.h>


int fchdir(int fd)
{
        if (fd<0)
                return -EINVAL;
        return syscall(__NR_fchdir, (long)fd, 0, 0);
}

