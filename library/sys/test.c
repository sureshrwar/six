
#include <syscall.h>
#include <linux/errno.h>

int test(long func)
{
	return syscall(6, (long)func, 0, 0);
}
