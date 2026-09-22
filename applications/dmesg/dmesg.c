#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern int klogctl(int type, char *buf, int len);

static char kbuf[16384];
static char obuf[16384];

int
main(int argc, char **argv)
{
	int type = 3; /* read all messages remaining in ring buffer */
	int n, i, olen = 0, bol = 1;

	if (argc > 1 && strcmp(argv[1], "-c") == 0)
		type = 4; /* read and clear all messages */

	memset(kbuf, 0, sizeof(kbuf));
	n = klogctl(type, kbuf, sizeof(kbuf) - 1);
	if (n < 0) {
		perror("dmesg: klogctl");
		return 1;
	}
	if (n == 0)
		return 0;
	for (i = 0; i < n; i++) {
		if (bol && i + 2 < n && kbuf[i] == '<' &&
		    kbuf[i + 1] >= '0' && kbuf[i + 1] <= '7' && kbuf[i + 2] == '>') {
			i += 2;
			bol = 0;
			continue;
		}
		obuf[olen++] = kbuf[i];
		bol = (kbuf[i] == '\n');
	}
	if (olen > 0 && obuf[olen - 1] != '\n')
		obuf[olen++] = '\n';
	if (olen > 0)
		write(1, obuf, olen);
	return 0;
}
