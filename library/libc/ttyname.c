#include <stdio.h>
#include <linux/types.h>
#include <linux/dirent.h>
#include <linux/limits.h>
#include <stat.h>

static char base[] = "/dev";
static char path[PATH_MAX];

char *ttyname(int fd)
{
	struct dirent *b;
	struct stat tty_stat;
	char aa[256], *a;
	int dd, ret, cur = 0;
	a = &aa[0];

	if(fstat(fd, &tty_stat)<0 || !S_ISCHR(tty_stat.st_mode))
		return NULL;

	dd = open(base, 0);
	if(dd < 0)
		return NULL;
	ret = getdents(dd, (struct dirent *)a, 256);
	while(cur < ret)
	{
		b = (struct dirent *)a;
		if(b->d_ino == tty_stat.st_ino)
		{
			strcpy(path, base);
			strcat(path, "/");
			strcat(path, b->d_name);
			return path;
		}
		a += b->d_reclen;
		cur += b->d_reclen;
	}
	printf("\n");
	close(dd);
}
