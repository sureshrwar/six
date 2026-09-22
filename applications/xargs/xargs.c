#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

extern char **environ;

static int
run_batch(char **base_argv, int base_argc, char items[][64], int nitems, int verbose)
{
	char *cargv[64];
	char path[128];
	int i, k = 0, status = 0;
	pid_t pid;

	for (i = 0; i < base_argc && k < 62; i++)
		cargv[k++] = base_argv[i];
	for (i = 0; i < nitems && k < 62; i++)
		cargv[k++] = items[i];
	cargv[k] = NULL;

	if (verbose) {
		for (i = 0; i < k; i++)
			printf("%s%s", i ? " " : "", cargv[i]);
		printf("\n");
	}

	pid = fork();
	if (pid == 0) {
		if (strchr(cargv[0], '/')) {
			execve(cargv[0], cargv, environ);
		} else {
			snprintf(path, sizeof(path), "/bin/%s", cargv[0]);
			execve(path, cargv, environ);
			snprintf(path, sizeof(path), "/usr/bin/%s", cargv[0]);
			execve(path, cargv, environ);
		}
		perror(cargv[0]);
		_exit(127);
	} else if (pid > 0) {
		waitpid(pid, &status, 0);
	}
	return status;
}

int
main(int argc, char **argv)
{
	int max_args = 32, verbose = 0, i = 1;
	char *def_cmd[1] = { "echo" };
	char **base_argv;
	int base_argc;
	char items[32][64];
	int nitems = 0, wlen = 0, n, k;
	char fbuf[256], word[64];

	while (i < argc && argv[i][0] == '-') {
		if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
			max_args = atoi(argv[i + 1]);
			if (max_args < 1 || max_args > 32)
				max_args = 32;
			i += 2;
		} else if (strcmp(argv[i], "-t") == 0) {
			verbose = 1;
			i++;
		} else {
			i++;
		}
	}

	if (i < argc) {
		base_argv = &argv[i];
		base_argc = argc - i;
	} else {
		base_argv = def_cmd;
		base_argc = 1;
	}

	while ((n = read(0, fbuf, sizeof(fbuf))) > 0) {
		for (k = 0; k < n; k++) {
			char c = fbuf[k];
			if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
				if (wlen > 0) {
					word[wlen] = '\0';
					strcpy(items[nitems++], word);
					wlen = 0;
					if (nitems >= max_args) {
						run_batch(base_argv, base_argc, items, nitems, verbose);
						nitems = 0;
					}
				}
			} else if (wlen < (int)sizeof(word) - 1) {
				word[wlen++] = c;
			}
		}
	}
	if (wlen > 0) {
		word[wlen] = '\0';
		strcpy(items[nitems++], word);
	}
	if (nitems > 0)
		run_batch(base_argv, base_argc, items, nitems, verbose);

	return 0;
}
