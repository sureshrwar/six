/*
 * telehack - quick launcher for telehack.com via telnet
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int execv(const char *path, char *const argv[]);

int main(int argc, char **argv)
{
	char *args[4];

	if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
		printf("Usage: telehack\n");
		printf("Connect to telehack.com over Telnet (port 23).\n");
		return 0;
	}

	args[0] = "telnet";
	args[1] = "telehack.com";
	args[2] = "23";
	args[3] = NULL;

	execv("/bin/telnet", args);
	printf("telehack: failed to execute /bin/telnet\n");
	return 1;
}
