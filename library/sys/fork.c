
#include <syscall.h>
#include <linux/errno.h>

int fork(void)
{
	return syscall(__NR_fork, 0, 0, 0);
}
