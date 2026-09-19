
#include <syscall.h>
#include <linux/errno.h>
#include <linux/types.h>


int fchmod(int fd, mode_t mode)
{
	if (fd<0)
                return -EINVAL;
        return syscall(__NR_fchmod, (long)fd, (long)mode, 0);
}

