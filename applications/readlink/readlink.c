#include <stdio.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	int no_nl = 0, canon = 0, i = 1, rc = 0;

	while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
		const char *p = argv[i] + 1;
		while (*p) {
			if (*p == 'n')
				no_nl = 1;
			else if (*p == 'f' || *p == 'e' || *p == 'm')
				canon = 1;
			p++;
		}
		i++;
	}

	if (i >= argc)
		return 1;

	for (; i < argc; i++) {
		char buf[256];
		int r = readlink(argv[i], buf, sizeof(buf) - 1);
		if (r < 0) {
			if (canon) {
				printf("%s%s", argv[i], no_nl ? "" : "\n");
				continue;
			}
			rc = 1;
			continue;
		}
		buf[r] = '\0';
		printf("%s%s", buf, no_nl ? "" : "\n");
	}
	return rc;
}
