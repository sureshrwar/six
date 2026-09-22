
#include <syscall.h>
#include <linux/errno.h>

int pause(void)
{
	return __syscall_return(syscall(__NR_pause, 0, 0, 0));
}
