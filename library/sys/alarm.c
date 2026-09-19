
#include <syscall.h>
#include <linux/errno.h>

int alarm(int secs)
{
	if(secs >= 0)
		return syscall(__NR_alarm, (long)secs, 0, 0);
	else
		return -EINVAL;
}
