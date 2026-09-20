/*
 * tail.c - Output the last part of files for SIX
 *
 * Options:
 *   -n count   Output the last count lines (default 10)
 *   -<number>  Shorthand for -n number
 *   -c bytes   Output the last bytes bytes
 *   -f         Follow file changes
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

static int opt_lines = 10;
static int opt_bytes = -1;
static int opt_follow = 0;

static char *read_line(FILE *fp)
{
	int cap = 128;
	int len = 0;
	char *buf = malloc(cap);
	if (!buf) return NULL;

	while (fgets(buf + len, cap - len, fp)) {
		len += strlen(buf + len);
		if (len > 0 && buf[len - 1] == '\n') {
			return buf;
		}
		if (len >= cap - 1) {
			cap *= 2;
			char *nb = realloc(buf, cap);
			if (!nb) {
				free(buf);
				return NULL;
			}
			buf = nb;
		}
	}

	if (len == 0) {
		free(buf);
		return NULL;
	}
	return buf;
}

static void tail_bytes(int fd, off_t bytes)
{
	struct stat st;
	if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) {
		off_t offset = (st.st_size > bytes) ? (st.st_size - bytes) : 0;
		lseek(fd, offset, SEEK_SET);
		char buf[4096];
		ssize_t n;
		while ((n = read(fd, buf, sizeof(buf))) > 0) {
			write(1, buf, n);
		}
	} else {
		/* Non-seekable stream: keep a circular buffer of bytes */
		char *buf = malloc(bytes);
		if (!buf) return;
		off_t pos = 0;
		off_t total = 0;
		char c;
		while (read(fd, &c, 1) > 0) {
			buf[pos] = c;
			pos = (pos + 1) % bytes;
			total++;
		}
		if (total <= bytes) {
			write(1, buf, total);
		} else {
			write(1, buf + pos, bytes - pos);
			if (pos > 0)
				write(1, buf, pos);
		}
		free(buf);
	}
}

static void tail_lines_stream(FILE *fp, int nlines)
{
	if (nlines <= 0) return;

	char **ring = calloc(nlines, sizeof(char *));
	if (!ring) return;

	int head = 0;
	int count = 0;
	char *line;

	while ((line = read_line(fp)) != NULL) {
		if (ring[head]) {
			free(ring[head]);
		}
		ring[head] = line;
		head = (head + 1) % nlines;
		if (count < nlines) count++;
	}

	int start = (head + nlines - count) % nlines;
	int i;
	for (i = 0; i < count; i++) {
		int idx = (start + i) % nlines;
		if (ring[idx]) {
			fputs(ring[idx], stdout);
			free(ring[idx]);
			ring[idx] = NULL;
		}
	}
	free(ring);
}

static void tail_lines_file(int fd, int nlines)
{
	struct stat st;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size == 0) {
		FILE *fp = fdopen(fd, "r");
		if (fp) tail_lines_stream(fp, nlines);
		return;
	}

	if (nlines <= 0) return;

	off_t cur = st.st_size;
	int found_lines = 0;
	char chunk[4096];

	/* If file ends with newline, do not count trailing empty newline as a line */
	lseek(fd, cur - 1, SEEK_SET);
	char lastc = 0;
	if (read(fd, &lastc, 1) == 1 && lastc == '\n') {
		cur--;
	}

	while (cur > 0 && found_lines < nlines) {
		off_t read_start = (cur >= sizeof(chunk)) ? (cur - sizeof(chunk)) : 0;
		size_t to_read = cur - read_start;
		lseek(fd, read_start, SEEK_SET);
		ssize_t n = read(fd, chunk, to_read);
		if (n <= 0) break;

		int i;
		for (i = n - 1; i >= 0; i--) {
			if (chunk[i] == '\n') {
				found_lines++;
				if (found_lines >= nlines) {
					cur = read_start + i + 1;
					break;
				}
			}
		}
		if (found_lines < nlines) {
			cur = read_start;
		}
	}

	lseek(fd, cur, SEEK_SET);
	ssize_t n;
	while ((n = read(fd, chunk, sizeof(chunk))) > 0) {
		write(1, chunk, n);
	}
}

static void do_follow(int fd)
{
	char buf[1024];
	while (1) {
		ssize_t n = read(fd, buf, sizeof(buf));
		if (n > 0) {
			write(1, buf, n);
		} else {
			sleep(1);
		}
	}
}

int main(int argc, char **argv)
{
	int arg_idx = 1;
	while (arg_idx < argc && argv[arg_idx][0] == '-' && argv[arg_idx][1] != '\0') {
		char *p = argv[arg_idx] + 1;
		if (strcmp(p, "-") == 0) {
			arg_idx++;
			break;
		}

		/* Check for -<number> */
		if (*p >= '0' && *p <= '9') {
			opt_lines = atoi(p);
			arg_idx++;
			continue;
		}

		while (*p) {
			if (*p == 'n') {
				if (*(p + 1)) {
					opt_lines = atoi(p + 1);
					break;
				} else if (arg_idx + 1 < argc) {
					opt_lines = atoi(argv[++arg_idx]);
					break;
				} else {
					fprintf(stderr, "tail: option requires an argument -- n\n");
					return 1;
				}
			} else if (*p == 'c') {
				if (*(p + 1)) {
					opt_bytes = atoi(p + 1);
					break;
				} else if (arg_idx + 1 < argc) {
					opt_bytes = atoi(argv[++arg_idx]);
					break;
				} else {
					fprintf(stderr, "tail: option requires an argument -- c\n");
					return 1;
				}
			} else if (*p == 'f') {
				opt_follow = 1;
			} else {
				fprintf(stderr, "tail: unrecognized option '-%c'\n", *p);
				return 1;
			}
			p++;
		}
		arg_idx++;
	}

	int num_files = argc - arg_idx;
	if (num_files <= 0) {
		/* Stdin */
		if (opt_bytes >= 0) {
			tail_bytes(0, opt_bytes);
		} else {
			tail_lines_stream(stdin, opt_lines);
		}
		if (opt_follow) {
			do_follow(0);
		}
	} else {
		int i;
		for (i = arg_idx; i < argc; i++) {
			int fd = -1;
			if (strcmp(argv[i], "-") == 0) {
				fd = 0;
			} else {
				fd = open(argv[i], O_RDONLY);
				if (fd < 0) {
					fprintf(stderr, "tail: cannot open %s\n", argv[i]);
					continue;
				}
			}

			if (num_files > 1) {
				printf("==> %s <==\n", argv[i]);
				fflush(stdout);
			}

			if (opt_bytes >= 0) {
				tail_bytes(fd, opt_bytes);
			} else {
				tail_lines_file(fd, opt_lines);
			}

			if (opt_follow && (i == argc - 1)) {
				do_follow(fd);
			}

			if (fd > 0) close(fd);
		}
	}

	return 0;
}
