
#include <syscall.h>
#include <linux/errno.h>
#include <errno.h>

int execve(char *filename, char **argv, char **envp)
{
	int ret;
	if (!filename)
                return __syscall_error(EINVAL);
	ret = syscall(__NR_execve, (long)filename, (long)argv, (long)envp);
	errno = (ret < 0) ? -ret : ENOENT;
	return -1;
}
