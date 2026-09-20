#include <syscall.h>
#include <errno.h>
#include <linux/types.h>
#include <linux/resource.h>

int wait4(pid_t pid, unsigned int *stat_addr, int options, struct rusage *ru)
{
	int ret = syscall(__NR_wait4, (long)pid, (long)stat_addr, (long)options);
	if (ret < 0) {
		errno = -ret;
		return -1;
	}
	return ret;
}
