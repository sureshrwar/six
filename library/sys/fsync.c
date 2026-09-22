
#include <syscall.h>
#include <linux/errno.h>


int fsync(int fd)
{
        return __syscall_return(syscall(__NR_fsync, (long)fd, 0, 0));
}

