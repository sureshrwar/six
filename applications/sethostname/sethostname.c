#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	if (argc == 1) {
		char h[256];
		if (gethostname(h, sizeof(h)) < 0) {
			perror("gethostname");
			return 1;
		}
		printf("%s\n", h);
		return 0;
	} else if (argc == 2) {
		if (sethostname(argv[1], strlen(argv[1])) < 0) {
			perror("sethostname");
			return 2;
		}
		return 0;
	} else {
		fprintf(stderr, "usage: hostname [name]\n");
		return 1;
	}
}
