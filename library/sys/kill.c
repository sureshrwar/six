
#include <syscall.h>
#include <linux/errno.h>

int kill(int pid, int sig)
{
	if(pid >=1)
		return __syscall_return(syscall(__NR_kill, (long)pid, (long)sig, 0));
	else
                return __syscall_error(EINVAL);
}
