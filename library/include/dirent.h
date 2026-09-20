#ifndef _DIRENT_H
#define _DIRENT_H

#include <linux/types.h>
#include <linux/dirent.h>

#define DIRBUF 1024

typedef struct {
	int   dd_fd;
	int   dd_loc;
	int   dd_size;
	char  dd_buf[DIRBUF];
} DIR;

DIR *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
void rewinddir(DIR *dirp);
int closedir(DIR *dirp);
int dirfd(DIR *dirp);

#ifndef d_fileno
#define d_fileno d_ino
#endif

#endif
