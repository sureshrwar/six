#include <stdlib.h>
#include <string.h>

extern char **envlist;
static int env_is_malloced = 0;

int putenv(char *string)
{
	char *eq;
	int namelen;
	int count = 0;
	int i;
	char **new_env;

	if (!string)
		return -1;

	eq = strchr(string, '=');
	if (!eq)
		return -1;

	namelen = eq - string;

	if (envlist) {
		for (i = 0; envlist[i] != NULL; i++) {
			if (strncmp(envlist[i], string, namelen) == 0 &&
			    envlist[i][namelen] == '=') {
				envlist[i] = string;
				return 0;
			}
		}
		count = i;
	}

	new_env = (char **)malloc((count + 2) * sizeof(char *));
	if (!new_env)
		return -1;

	for (i = 0; i < count; i++)
		new_env[i] = envlist[i];

	new_env[count] = string;
	new_env[count + 1] = NULL;

	if (env_is_malloced)
		free(envlist);

	envlist = new_env;
	env_is_malloced = 1;
	return 0;
}
