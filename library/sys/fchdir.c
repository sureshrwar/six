
#include <syscall.h>
#include <linux/errno.h>


int fchdir(int fd)
{
        if (fd<0)
                return __syscall_error(EINVAL);
        return __syscall_return(syscall(__NR_fchdir, (long)fd, 0, 0));
}

