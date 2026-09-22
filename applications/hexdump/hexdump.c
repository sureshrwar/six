#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

static void
dump_fd(int fd, int canonical, long skip, long max_len)
{
	unsigned char buf[16];
	unsigned long off = 0;
	int n, i;

	if (skip > 0) {
		if (lseek(fd, (off_t)skip, SEEK_SET) >= 0) {
			off = (unsigned long)skip;
		} else {
			while (off < (unsigned long)skip) {
				int r = read(fd, (char *)buf, 1);
				if (r <= 0)
					return;
				off++;
			}
		}
	}

	while ((n = read(fd, (char *)buf, 16)) > 0) {
		if (max_len >= 0 && (long)(off - (unsigned long)(skip > 0 ? skip : 0) + (unsigned long)n) > max_len) {
			n = (int)(max_len - (long)(off - (unsigned long)(skip > 0 ? skip : 0)));
			if (n <= 0)
				break;
		}
		if (canonical) {
			printf("%08lx  ", off);
			for (i = 0; i < 16; i++) {
				if (i < n)
					printf("%02x ", buf[i]);
				else
					printf("   ");
				if (i == 7)
					printf(" ");
			}
			printf(" |");
			for (i = 0; i < n; i++) {
				unsigned char c = buf[i];
				printf("%c", (c >= 32 && c < 127) ? (char)c : '.');
			}
			printf("|\n");
		} else {
			printf("%07lx", off);
			for (i = 0; i < n; i += 2) {
				unsigned int w = buf[i];
				if (i + 1 < n)
					w |= ((unsigned int)buf[i + 1] << 8);
				printf(" %04x", w);
			}
			printf("\n");
		}
		off += (unsigned long)n;
		if (max_len >= 0 && (long)(off - (unsigned long)(skip > 0 ? skip : 0)) >= max_len)
			break;
	}
	if (canonical)
		printf("%08lx\n", off);
	else
		printf("%07lx\n", off);
}

int
main(int argc, char **argv)
{
	int canonical = 0, i = 1;
	long skip = 0, max_len = -1;
	const char *prog = strrchr(argv[0], '/');
	prog = prog ? prog + 1 : argv[0];

	if (strcmp(prog, "hd") == 0 || strcmp(prog, "xxd") == 0)
		canonical = 1;

	while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
		if (strcmp(argv[i], "-C") == 0) {
			canonical = 1;
			i++;
		} else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
			max_len = atol(argv[i + 1]);
			i += 2;
		} else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
			skip = atol(argv[i + 1]);
			i += 2;
		} else {
			i++;
		}
	}

	if (i >= argc) {
		dump_fd(0, canonical, skip, max_len);
	} else {
		for (; i < argc; i++) {
			int fd = open(argv[i], 0);
			if (fd < 0) {
				perror(argv[i]);
				return 1;
			}
			dump_fd(fd, canonical, skip, max_len);
			close(fd);
		}
	}
	return 0;
}
