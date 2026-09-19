
#include <syscall.h>
#include <linux/errno.h>

int kill(int pid, int sig)
{
	if(pid >=1)
		return syscall(__NR_kill, (long)pid, (long)sig, 0);
	else
                return -EINVAL;
}
