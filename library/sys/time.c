
#include <syscall.h>
#include <linux/errno.h>


int time(int *secs)
{
	return __syscall_return(syscall(__NR_time, (long)secs, 0, 0));
}
