
#include <syscall.h>
#include <linux/errno.h>


int waitpid(int pid, unsigned int *staddr, int options)
{
	return syscall(__NR_waitpid, (long)pid, (long)staddr, (long)options);
}

int wait(unsigned int *staddr)
{
	return syscall(__NR_waitpid, (long)-1, (long)staddr, (long)0);
}
