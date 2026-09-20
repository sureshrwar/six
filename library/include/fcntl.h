#ifndef _FCNTL_H
#define _FCNTL_H

#include <linux/types.h>
#include <linux/fcntl.h>

int open(const char *pathname, int flags, ...);
int creat(const char *pathname, mode_t mode);
int fcntl(int fd, int cmd, ...);

#endif
