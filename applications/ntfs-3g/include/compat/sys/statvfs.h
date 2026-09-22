#ifndef _COMPAT_SYS_STATVFS_H
#define _COMPAT_SYS_STATVFS_H
#include <sys/types.h>
struct statvfs {
    unsigned long f_bsize;
    unsigned long f_frsize;
    unsigned long long f_blocks;
    unsigned long long f_bfree;
    unsigned long long f_bavail;
    unsigned long long f_files;
    unsigned long long f_ffree;
    unsigned long long f_favail;
    unsigned long f_fsid;
    unsigned long f_flag;
    unsigned long f_namemax;
};
#define ST_RDONLY 1
#define ST_NOSUID 2
int statvfs(const char *path, struct statvfs *buf);
int fstatvfs(int fd, struct statvfs *buf);
#endif
