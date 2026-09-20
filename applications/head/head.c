/*
 * head.c - Output the first part of files for SIX (/bin/head)
 */

#include <stdio.h>
#include <stdlib.h>
#include <linux/string.h>
#include <linux/unistd.h>
#include <linux/fcntl.h>

static void head_fd(int fd, int max_lines)
{
	char ch;
	int lines = 0;

	while (lines < max_lines && read(fd, &ch, 1) == 1) {
		putchar(ch);
		if (ch == '\n')
			lines++;
	}
}

int main(int argc, char **argv)
{
	int max_lines = 10;
	int file_count = 0;
	int files_start = 0;
	int i;

	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			if (argv[i][1] == 'n' && argv[i][2] != '\0')
				max_lines = atoi(&argv[i][2]);
			else if (argv[i][1] == 'n' && i + 1 < argc)
				max_lines = atoi(argv[++i]);
			else if (argv[i][1] >= '0' && argv[i][1] <= '9')
				max_lines = atoi(&argv[i][1]);
		} else {
			if (!files_start) files_start = i;
			file_count++;
		}
	}

	if (max_lines <= 0) max_lines = 10;

	if (file_count == 0) {
		head_fd(0, max_lines);
		return 0;
	}

	for (i = files_start; i < argc; i++) {
		if (argv[i][0] == '-')
			continue;

		int fd = open(argv[i], O_RDONLY, 0);
		if (fd < 0) {
			perror(argv[i]);
			continue;
		}

		if (file_count > 1)
			printf("==> %s <==\n", argv[i]);

		head_fd(fd, max_lines);
		close(fd);
	}

	return 0;
}
