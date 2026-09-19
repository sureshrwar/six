#include <stdio.h>
#include <stdlib.h>

extern char **envlist;
char **environ = 0;

int execle(const char *path, const char *arg0, ...)
{
	char **argv = (char **)&arg0;
	char **p = argv;
	while (*p != 0)
		p++;
	p++; /* envp follows NULL */
	return execve(path, argv, *p ? (char **)*p : envlist);
}

char *mktemp(char *template)
{
	static int seq = 0;
	int len, pid, i, val;
	if (!template)
		return 0;
	len = strlen(template);
	val = (getpid() * 100) + (++seq);
	for (i = len - 1; i >= 0 && template[i] == 'X'; i--) {
		template[i] = '0' + (val % 10);
		val /= 10;
	}
	return template;
}
