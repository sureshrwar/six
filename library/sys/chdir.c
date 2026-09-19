
#include <syscall.h>
#include <linux/errno.h>


int chdir(char *dir)
{
	if (!dir)
		return -EINVAL;
	return syscall(__NR_chdir, (long)dir, 0, 0);
}
