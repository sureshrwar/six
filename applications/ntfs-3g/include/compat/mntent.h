#ifndef _COMPAT_MNTENT_H
#define _COMPAT_MNTENT_H
#include <stdio.h>
#define MOUNTED "/etc/mtab"
struct mntent {
    char *mnt_fsname;
    char *mnt_dir;
    char *mnt_type;
    char *mnt_opts;
    int mnt_freq;
    int mnt_passno;
};
FILE *setmntent(const char *filename, const char *type);
struct mntent *getmntent(FILE *stream);
int addmntent(FILE *stream, const struct mntent *mnt);
int endmntent(FILE *streamp);
char *hasmntopt(const struct mntent *mnt, const char *opt);
#endif
