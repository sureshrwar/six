
#include <stdio.h>
#include <linux/limits.h>


main(int argc, char *argv[])
{
	int len;
	char dir[PATH_MAX];
	char *d;
	memset(dir, 0, PATH_MAX);
	d = getcwd(dir, 256);
	if (!d)
	{
		write(1, "failure\n", 8);
		exit(1);
	}
	len = strlen(d);
	d[len] = '\n';
	write(1, d, strlen(d));
}
