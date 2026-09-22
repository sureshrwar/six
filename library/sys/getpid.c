
#include <syscall.h>
#include <linux/errno.h>

int getpid()
{
	return __syscall_return(syscall(__NR_getpid, 0, 0, 0));
}
