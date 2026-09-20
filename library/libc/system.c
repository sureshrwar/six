#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

int system(const char *command)
{
	pid_t pid;
	int status;

	if (!command)
		return 1;

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		char *argv[4];
		argv[0] = "sh";
		argv[1] = "-c";
		argv[2] = (char *)command;
		argv[3] = NULL;
		execve("/bin/sh", argv, NULL);
		_exit(127);
	}

	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR)
			return -1;
	}
	return status;
}
