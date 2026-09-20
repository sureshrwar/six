#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/types.h>
#include <linux/dirent.h>
#include <linux/limits.h>
#include <stat.h>

static char base[] = "/dev";
static char path[PATH_MAX];

extern int getdents(int fd, void *buf, int count);

char *ttyname(int fd)
{
	struct stat tty_stat;
	char buf[512];
	int dd, ret;

	if (fstat(fd, &tty_stat) < 0 || !S_ISCHR(tty_stat.st_mode))
		return NULL;

	dd = open(base, 0);
	if (dd < 0)
		return NULL;

	while ((ret = getdents(dd, (struct dirent *)buf, sizeof(buf))) > 0) {
		int cur = 0;
		while (cur < ret) {
			struct dirent *b = (struct dirent *)(buf + cur);
			if (b->d_reclen == 0)
				break;
			if (b->d_ino == tty_stat.st_ino) {
				strcpy(path, base);
				strcat(path, "/");
				strcat(path, b->d_name);
				close(dd);
				return path;
			}
			cur += b->d_reclen;
		}
	}

	close(dd);
	return NULL;
}
