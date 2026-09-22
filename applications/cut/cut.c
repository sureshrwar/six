#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

static int sel[256];

static void
parse_list(const char *spec)
{
	const char *p = spec;
	while (*p) {
		int a = 0, b = 0, i;
		if (*p == '-') {
			a = 1;
		} else {
			while (*p >= '0' && *p <= '9')
				a = a * 10 + (*p++ - '0');
		}
		if (*p == '-') {
			p++;
			if (*p >= '0' && *p <= '9') {
				while (*p >= '0' && *p <= '9')
					b = b * 10 + (*p++ - '0');
			} else {
				b = 255;
			}
		} else {
			b = a;
		}
		if (a < 1) a = 1;
		if (b > 255) b = 255;
		for (i = a; i <= b; i++)
			sel[i] = 1;
		if (*p == ',')
			p++;
		else if (*p)
			p++;
	}
}

static void
cut_line(const char *line, int mode_char, char delim)
{
	if (mode_char) {
		int idx = 1;
		const char *p = line;
		while (*p) {
			if (idx < 256 && sel[idx])
				printf("%c", *p);
			p++;
			idx++;
		}
		printf("\n");
	} else {
		int fidx = 1, first = 1;
		const char *p = line;
		if (strchr(line, delim) == NULL) {
			printf("%s\n", line);
			return;
		}
		while (1) {
			const char *next = strchr(p, delim);
			int len = next ? (int)(next - p) : (int)strlen(p);
			if (fidx < 256 && sel[fidx]) {
				if (!first)
					printf("%c", delim);
				printf("%.*s", len, p);
				first = 0;
			}
			if (!next)
				break;
			p = next + 1;
			fidx++;
		}
		printf("\n");
	}
}

static void
cut_fd(int fd, int mode_char, char delim)
{
	char fbuf[512], lbuf[512];
	int n, i, lpos = 0;
	while ((n = read(fd, fbuf, sizeof(fbuf))) > 0) {
		for (i = 0; i < n; i++) {
			if (fbuf[i] == '\n') {
				lbuf[lpos] = '\0';
				cut_line(lbuf, mode_char, delim);
				lpos = 0;
			} else if (fbuf[i] != '\r' && lpos < (int)sizeof(lbuf) - 1) {
				lbuf[lpos++] = fbuf[i];
			}
		}
	}
	if (lpos > 0) {
		lbuf[lpos] = '\0';
		cut_line(lbuf, mode_char, delim);
	}
}

int
main(int argc, char **argv)
{
	int mode_char = 0, i = 1;
	char delim = '\t';

	memset(sel, 0, sizeof(sel));
	while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
		if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
			delim = argv[i + 1][0];
			i += 2;
		} else if (strncmp(argv[i], "-d", 2) == 0 && argv[i][2] != '\0') {
			delim = argv[i][2];
			i++;
		} else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
			mode_char = 0;
			parse_list(argv[i + 1]);
			i += 2;
		} else if (strncmp(argv[i], "-f", 2) == 0 && argv[i][2] != '\0') {
			mode_char = 0;
			parse_list(argv[i] + 2);
			i++;
		} else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
			mode_char = 1;
			parse_list(argv[i + 1]);
			i += 2;
		} else if (strncmp(argv[i], "-c", 2) == 0 && argv[i][2] != '\0') {
			mode_char = 1;
			parse_list(argv[i] + 2);
			i++;
		} else {
			i++;
		}
	}

	if (i >= argc) {
		cut_fd(0, mode_char, delim);
	} else {
		for (; i < argc; i++) {
			int fd = open(argv[i], 0);
			if (fd < 0) {
				perror(argv[i]);
				return 1;
			}
			cut_fd(fd, mode_char, delim);
			close(fd);
		}
	}
	return 0;
}
