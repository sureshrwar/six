
#include <syscall.h>
#include <linux/errno.h>
#include <errno.h>

int execve(char *filename, char **argv, char **envp)
{
	if (!filename)
                return -EINVAL;
	syscall(__NR_execve, (long)filename, (long)argv, (long)envp);
	errno = ENOENT;
	return -1;
}
