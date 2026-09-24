#ifndef _SIX_COMPAT_H
#define _SIX_COMPAT_H

#include <sys/types.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/sysmacros.h>

#ifndef __timespec_defined
#define __timespec_defined 1
#endif

typedef int sig_atomic_t;

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif

#ifndef S_IFSOCK
#define S_IFSOCK 0140000
#endif

#ifndef S_ISLNK
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#ifndef S_ISCHR
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#endif
#ifndef S_ISBLK
#define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
#endif
#ifndef S_ISFIFO
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#endif
#ifndef S_ISSOCK
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
#endif

#ifndef S_IREAD
#define S_IREAD S_IRUSR
#endif
#ifndef S_IWRITE
#define S_IWRITE S_IWUSR
#endif
#ifndef S_IEXEC
#define S_IEXEC S_IXUSR
#endif
#ifndef ENOTSUP
#define ENOTSUP EOPNOTSUPP
#endif

#ifndef MS_DIRSYNC
#define MS_DIRSYNC 128
#endif
#ifndef MS_NOATIME
#define MS_NOATIME 1024
#endif
#ifndef MS_NODIRATIME
#define MS_NODIRATIME 2048
#endif
#ifndef MS_BIND
#define MS_BIND 4096
#endif
#ifndef MNT_DETACH
#define MNT_DETACH 2
#endif

#ifndef _SC_PAGESIZE
#define _SC_PAGESIZE 30
#endif
#ifndef _SC_PAGE_SIZE
#define _SC_PAGE_SIZE _SC_PAGESIZE
#endif

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif

#ifndef BLKROGET
#define BLKROGET   _IO(0x12,94)
#endif
#ifndef BLKGETSIZE
#define BLKGETSIZE _IO(0x12,96)
#endif
#ifndef BLKFLSBUF
#define BLKFLSBUF  _IO(0x12,97)
#endif

#ifndef MB_CUR_MAX
#define MB_CUR_MAX 4
#endif

typedef unsigned long fsblkcnt_t;
typedef unsigned long fsfilcnt_t;

ssize_t pread(int fd, void *buf, size_t count, long long offset);
ssize_t pwrite(int fd, const void *buf, size_t count, long long offset);
int fdatasync(int fd);
int daemon(int nochdir, int noclose);
int getpagesize(void);
long sysconf(int name);
char *realpath(const char *path, char *resolved_path);
char *mkdtemp(char *template);
char *strsep(char **stringp, const char *delim);
char *stpcpy(char *dest, const char *src);
long random(void);
void srandom(unsigned int seed);
int umount2(const char *target, int flags);
int clock_gettime(int clk_id, struct timespec *tp);
int getresuid(uid_t *ruid, uid_t *euid, uid_t *suid);
int getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid);
int setresuid(uid_t ruid, uid_t euid, uid_t suid);
int setresgid(gid_t rgid, gid_t egid, gid_t sgid);

#endif /* _SIX_COMPAT_H */
