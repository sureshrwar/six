#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern int symlink(const char *oldpath, const char *newpath);

int
main(int argc, char **argv)
{
	int sym = 0, force = 0, i = 1;

	while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
		const char *p = argv[i] + 1;
		while (*p) {
			if (*p == 's')
				sym = 1;
			else if (*p == 'f')
				force = 1;
			else {
				printf("Usage: ln [-sf] target link_name\n");
				return 1;
			}
			p++;
		}
		i++;
	}

	if (argc - i != 2) {
		printf("Usage: ln [-sf] target link_name\n");
		return 1;
	}

	if (force)
		unlink(argv[i + 1]);

	if (sym) {
		if (symlink(argv[i], argv[i + 1]) < 0) {
			perror("ln");
			return 1;
		}
	} else {
		if (link(argv[i], argv[i + 1]) < 0) {
			perror("ln");
			return 1;
		}
	}
	return 0;
}
