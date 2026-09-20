/*
 * wc.c - Word, line, and byte counter for SIX (/bin/wc)
 */

#include <stdio.h>
#include <stdlib.h>
#include <linux/string.h>
#include <linux/unistd.h>
#include <linux/fcntl.h>

#define BUF_SIZE 4096

static int opt_l = 0, opt_w = 0, opt_c = 0;

static void count_fd(int fd, const char *name, long *tot_l, long *tot_w, long *tot_c)
{
	char buf[BUF_SIZE];
	long lines = 0, words = 0, bytes = 0;
	int in_word = 0;
	int n;

	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		int i;
		bytes += n;
		for (i = 0; i < n; i++) {
			char ch = buf[i];
			if (ch == '\n') lines++;
			if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
				in_word = 0;
			} else if (!in_word) {
				in_word = 1;
				words++;
			}
		}
	}

	*tot_l += lines;
	*tot_w += words;
	*tot_c += bytes;

	if (opt_l) printf(" %7ld", lines);
	if (opt_w) printf(" %7ld", words);
	if (opt_c) printf(" %7ld", bytes);
	if (name) printf(" %s", name);
	printf("\n");
}

int main(int argc, char **argv)
{
	long tot_l = 0, tot_w = 0, tot_c = 0;
	int files_start = 0;
	int file_count = 0;
	int i;

	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-' && argv[i][1] != '\0') {
			int j;
			for (j = 1; argv[i][j]; j++) {
				if (argv[i][j] == 'l') opt_l = 1;
				else if (argv[i][j] == 'w') opt_w = 1;
				else if (argv[i][j] == 'c' || argv[i][j] == 'm') opt_c = 1;
			}
		} else {
			if (!files_start) files_start = i;
			file_count++;
		}
	}

	/* Default: show lines, words, bytes */
	if (!opt_l && !opt_w && !opt_c) {
		opt_l = opt_w = opt_c = 1;
	}

	if (file_count == 0) {
		count_fd(0, NULL, &tot_l, &tot_w, &tot_c);
		return 0;
	}

	for (i = files_start; i < argc; i++) {
		if (argv[i][0] == '-' && argv[i][1] != '\0')
			continue;
		int fd = open(argv[i], O_RDONLY, 0);
		if (fd < 0) {
			perror(argv[i]);
			continue;
		}
		count_fd(fd, argv[i], &tot_l, &tot_w, &tot_c);
		close(fd);
	}

	if (file_count > 1) {
		if (opt_l) printf(" %7ld", tot_l);
		if (opt_w) printf(" %7ld", tot_w);
		if (opt_c) printf(" %7ld", tot_c);
		printf(" total\n");
	}

	return 0;
}
