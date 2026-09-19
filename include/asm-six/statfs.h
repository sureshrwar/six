
#ifndef _I386_STATFS_H
#define _I386_STATFS_H

#ifndef __KERNEL_STRICT_NAMES
 
#include <linux/types.h>

#if (!SIX)
typedef struct {
	int val[2];
} __kernel_fsid_t;
 
typedef __kernel_fsid_t fsid_t;
#endif

#endif

struct statfs {
        long f_type;    
        long f_bsize;   
        long f_blocks;  
        long f_bfree;   
        long f_bavail;  
        long f_files;   
        long f_ffree;   
        __kernel_fsid_t f_fsid;
        long f_namelen; 
        long f_spare[6];
};

#endif

