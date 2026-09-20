#include <syscall.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/dirent.h>

int __old_readdir(int fd, void *d, int count)
{
	if (fd < 0 || !d || count < 0)
		return -EINVAL;
	return syscall(__NR_readdir, (long)fd, (long)d, (long)count);
}
