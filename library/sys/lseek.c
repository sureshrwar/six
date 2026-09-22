
#include <syscall.h>
#include <linux/errno.h>


int lseek(int fd, long off, int origin)
{
	if (fd<0 || origin<0)
                return __syscall_error(EINVAL);
        return __syscall_return(syscall(__NR_lseek, (long)fd, off, (long)origin));
}

