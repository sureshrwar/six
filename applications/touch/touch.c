/*
 * touch.c - Touch / create files for SIX (/bin/touch)
 */

#include <stdio.h>
#include <stdlib.h>
#include <linux/unistd.h>
#include <linux/fcntl.h>
#include <stat.h>

int main(int argc, char **argv)
{
	int i, rc = 0;

	if (argc < 2) {
		fprintf(stderr, "Usage: touch <file...>\n");
		return 1;
	}

	for (i = 1; i < argc; i++) {
		int fd = open(argv[i], O_WRONLY | O_CREAT, 0644);
		if (fd < 0) {
			perror(argv[i]);
			rc = 1;
		} else {
			close(fd);
		}
	}

	return rc;
}
