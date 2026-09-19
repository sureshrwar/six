
#include <syscall.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/dirent.h>


int readdir(int fd, struct direct *d, int count)
{
	if (fd<0 || !d || count<0)
                return -EINVAL;
        return syscall(__NR_readdir, 0, 0, 0);
}

