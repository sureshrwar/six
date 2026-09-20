#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

extern FILE *fdopen(int fd, const char *mode);

FILE *popen(const char *command, const char *type)
{
	int pfd[2];
	pid_t pid;

	if (!command || !type)
		return NULL;

	if (pipe(pfd) < 0)
		return NULL;

	pid = fork();
	if (pid < 0) {
		close(pfd[0]);
		close(pfd[1]);
		return NULL;
	}

	if (pid == 0) {
		char *argv[4];
		if (type[0] == 'r') {
			close(pfd[0]);
			dup2(pfd[1], 1);
			close(pfd[1]);
		} else {
			close(pfd[1]);
			dup2(pfd[0], 0);
			close(pfd[0]);
		}
		argv[0] = "sh";
		argv[1] = "-c";
		argv[2] = (char *)command;
		argv[3] = NULL;
		execve("/bin/sh", argv, NULL);
		_exit(127);
	}

	if (type[0] == 'r') {
		close(pfd[1]);
		return fdopen(pfd[0], "r");
	} else {
		close(pfd[0]);
		return fdopen(pfd[1], "w");
	}
}

int pclose(FILE *stream)
{
	if (!stream)
		return -1;
	fclose(stream);
	return 0;
}
