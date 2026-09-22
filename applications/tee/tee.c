#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	int append = 0, i = 1, nfds = 0, k, n;
	int fds[16];
	char buf[512];

	if (i < argc && strcmp(argv[i], "-a") == 0) {
		append = 1;
		i++;
	}

	for (; i < argc && nfds < 16; i++) {
		int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
		int fd = open(argv[i], flags, 0644);
		if (fd < 0)
			perror(argv[i]);
		else
			fds[nfds++] = fd;
	}

	while ((n = read(0, buf, sizeof(buf))) > 0) {
		write(1, buf, n);
		for (k = 0; k < nfds; k++)
			write(fds[k], buf, n);
	}

	for (k = 0; k < nfds; k++)
		close(fds[k]);
	return 0;
}
