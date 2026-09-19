
#include <syscall.h>
#include <linux/errno.h>


int time(int *secs)
{
	return syscall(__NR_time, (long)secs, 0, 0);
}
