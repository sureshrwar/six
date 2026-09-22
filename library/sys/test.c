
#include <syscall.h>
#include <linux/errno.h>

int test(long func)
{
	return __syscall_return(syscall(6, (long)func, 0, 0));
}
