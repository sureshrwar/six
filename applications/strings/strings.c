#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

static void
strings_fd(int fd, int min_len)
{
	unsigned char fbuf[512];
	char sbuf[256];
	int slen = 0, n, i;

	while ((n = read(fd, (char *)fbuf, sizeof(fbuf))) > 0) {
		for (i = 0; i < n; i++) {
			unsigned char c = fbuf[i];
			if ((c >= 32 && c < 127) || c == '\t') {
				if (slen < (int)sizeof(sbuf) - 1)
					sbuf[slen++] = (char)c;
			} else {
				if (slen >= min_len) {
					sbuf[slen] = '\0';
					printf("%s\n", sbuf);
				}
				slen = 0;
			}
		}
	}
	if (slen >= min_len) {
		sbuf[slen] = '\0';
		printf("%s\n", sbuf);
	}
}

int
main(int argc, char **argv)
{
	int min_len = 4, i = 1;

	while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
		if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
			min_len = atoi(argv[i + 1]);
			i += 2;
		} else if (argv[i][1] >= '1' && argv[i][1] <= '9') {
			min_len = atoi(argv[i] + 1);
			i++;
		} else {
			i++;
		}
	}
	if (min_len < 1)
		min_len = 4;

	if (i >= argc) {
		strings_fd(0, min_len);
	} else {
		for (; i < argc; i++) {
			int fd = open(argv[i], 0);
			if (fd < 0) {
				perror(argv[i]);
				return 1;
			}
			strings_fd(fd, min_len);
			close(fd);
		}
	}
	return 0;
}
