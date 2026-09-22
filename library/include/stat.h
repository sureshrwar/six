
#ifndef STAT_H
#define STAT_H

#include <asm/stat.h>
#include <linux/stat.h>

struct stat {
        unsigned short st_dev;
        unsigned long  st_ino;
        unsigned short st_mode;
        unsigned short st_nlink;
        unsigned short st_uid;
        unsigned short st_gid;
        unsigned short st_rdev;
        unsigned long  st_size;
        unsigned long  st_atime;
        unsigned long  st_mtime;
        unsigned long  st_ctime;
        unsigned long  st_blksize;
        unsigned long  st_blocks;
};      


#endif
