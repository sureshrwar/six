#include <syscall.h>
#include <errno.h>

int waitpid(int pid, unsigned int *staddr, int options)
{
	int ret = syscall(__NR_waitpid, (long)pid, (long)staddr, (long)options);
	if (ret < 0) {
		errno = -ret;
		return -1;
	}
	return ret;
}

int wait(unsigned int *staddr)
{
	return waitpid(-1, staddr, 0);
}
