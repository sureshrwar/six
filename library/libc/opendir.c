#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stat.h>
#include <errno.h>

extern int getdents(int fd, void *buf, int count);

DIR *opendir(const char *name)
{
	struct stat st;
	int fd;
	DIR *dirp;

	if (!name) {
		errno = ENOENT;
		return NULL;
	}

	fd = open(name, O_RDONLY);
	if (fd < 0)
		return NULL;

	if (fstat(fd, &st) < 0) {
		close(fd);
		return NULL;
	}

	if (!S_ISDIR(st.st_mode)) {
		close(fd);
		errno = ENOTDIR;
		return NULL;
	}

	dirp = (DIR *)malloc(sizeof(DIR));
	if (!dirp) {
		close(fd);
		errno = ENOMEM;
		return NULL;
	}

	dirp->dd_fd = fd;
	dirp->dd_loc = 0;
	dirp->dd_size = 0;
	return dirp;
}

struct dirent *readdir(DIR *dirp)
{
	struct dirent *dp;

	if (!dirp || dirp->dd_fd < 0) {
		errno = EBADF;
		return NULL;
	}

	for (;;) {
		if (dirp->dd_loc >= dirp->dd_size) {
			dirp->dd_loc = 0;
			dirp->dd_size = getdents(dirp->dd_fd, dirp->dd_buf, sizeof(dirp->dd_buf));
			if (dirp->dd_size <= 0)
				return NULL;
		}

		dp = (struct dirent *)(dirp->dd_buf + dirp->dd_loc);
		if (dp->d_reclen == 0)
			return NULL;

		dirp->dd_loc += dp->d_reclen;

		if (dp->d_ino != 0)
			return dp;
	}
}

void rewinddir(DIR *dirp)
{
	if (!dirp || dirp->dd_fd < 0)
		return;

	lseek(dirp->dd_fd, 0, SEEK_SET);
	dirp->dd_loc = 0;
	dirp->dd_size = 0;
}

int closedir(DIR *dirp)
{
	int fd;

	if (!dirp) {
		errno = EBADF;
		return -1;
	}

	fd = dirp->dd_fd;
	free(dirp);
	return close(fd);
}

int dirfd(DIR *dirp)
{
	if (!dirp) {
		errno = EBADF;
		return -1;
	}
	return dirp->dd_fd;
}
