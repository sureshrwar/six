
#include <syscall.h>
#include <linux/errno.h>


int flock(int fd, int cmd)
{
        if (fd<0)
                return -EINVAL;
        return syscall(__NR_flock, (long)fd, (long)cmd, 0);
}

