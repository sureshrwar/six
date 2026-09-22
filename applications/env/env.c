#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

int
main(int argc, char **argv, char **envp)
{
	char *new_env[128];
	int nenv = 0, i = 1, ignore_env = 0;

	if (i < argc && strcmp(argv[i], "-i") == 0) {
		ignore_env = 1;
		i++;
	}

	if (!ignore_env && envp) {
		int k;
		for (k = 0; envp[k] && nenv < 120; k++)
			new_env[nenv++] = envp[k];
	}

	while (i < argc && strchr(argv[i], '=') != NULL) {
		char *eq = strchr(argv[i], '=');
		int k, klen = (int)(eq - argv[i]);
		int replaced = 0;
		for (k = 0; k < nenv; k++) {
			if (strncmp(new_env[k], argv[i], klen) == 0 && new_env[k][klen] == '=') {
				new_env[k] = argv[i];
				replaced = 1;
				break;
			}
		}
		if (!replaced && nenv < 126)
			new_env[nenv++] = argv[i];
		i++;
	}
	new_env[nenv] = NULL;

	if (i >= argc) {
		int k;
		for (k = 0; k < nenv; k++)
			printf("%s\n", new_env[k]);
		return 0;
	}

	{
		char cmd[128];
		if (strchr(argv[i], '/')) {
			execve(argv[i], &argv[i], new_env);
		} else {
			snprintf(cmd, sizeof(cmd), "/bin/%s", argv[i]);
			execve(cmd, &argv[i], new_env);
			snprintf(cmd, sizeof(cmd), "/usr/bin/%s", argv[i]);
			execve(cmd, &argv[i], new_env);
		}
		perror(argv[i]);
		return 127;
	}
}
