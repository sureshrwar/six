/*
 * rmdir.c - Remove empty directories for SIX (/bin/rmdir)
 */

#include <stdio.h>
#include <stdlib.h>
#include <linux/unistd.h>

int main(int argc, char **argv)
{
	int i, rc = 0;

	if (argc < 2) {
		fprintf(stderr, "Usage: rmdir <directory...>\n");
		return 1;
	}

	for (i = 1; i < argc; i++) {
		if (rmdir(argv[i]) != 0) {
			perror(argv[i]);
			rc = 1;
		}
	}

	return rc;
}
