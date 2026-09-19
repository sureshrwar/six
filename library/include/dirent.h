
#ifndef _LIBRARY_DIRENT_H
#define _LIBRARY_DIRENT_H

#include <linux/types.h>
#include <linux/dirent.h>

struct _fl_direct {             /* First slot in an entry */
        ino_t           d_ino;
        unsigned char   d_extent;
        char            d_name[5];  /* Four characters for the shortest name */
};      

#define _EXTENT(len)    (((len) + 3) >> 3)
        
struct _v7_direct {             
        ino_t           d_ino;
        char            d_name[14]; 
};

typedef struct {
        char            _fd;    /* Filedescriptor of open directory */
        char            _v7;    /* Directory is Version 7 */
        short           _count; /* This many objects in buf */
        off_t           _pos;   /* Position in directory file */
        struct _fl_direct  *_ptr;       /* Next slot in buf */
        struct _fl_direct  _buf[128];   /* One block of a directory file */
        struct _fl_direct  _v7f[3];     /* V7 entry transformed to flex */
} DIR;          

#endif
