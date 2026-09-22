
#include <syscall.h>
#include <linux/errno.h>

int alarm(int secs)
{
	if(secs >= 0)
		return __syscall_return(syscall(__NR_alarm, (long)secs, 0, 0));
	else
		return __syscall_error(EINVAL);
}
