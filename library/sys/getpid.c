
#include <syscall.h>
#include <linux/errno.h>

int getpid()
{
	return syscall(__NR_getpid, 0, 0, 0);
}
