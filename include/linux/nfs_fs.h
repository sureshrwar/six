
#ifndef _LINUX_NFS_FS_H
#define _LINUX_NFS_FS_H
 
/* 
 *  linux/include/linux/nfs_fs.h
 * 
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  OS-specific nfs filesystem definitions and declarations
 */

#include <linux/nfs.h>          

#include <linux/in.h>
#include <linux/nfs_mount.h>    

/*
 * The readdir cache size controls how many directory entries are cached.
 * Its size is limited by the number of nfs_entry structures that can fit
 * in one page, currently, the limit is 256 when using 4KB pages.
 */

#define NFS_READDIR_CACHE_SIZE          64

#define NFS_MAX_FILE_IO_BUFFER_SIZE     16384
#define NFS_DEF_FILE_IO_BUFFER_SIZE     1024 
        
/*      
 * The upper limit on timeouts for the exponential backoff algorithm.
 */     
        
#define NFS_MAX_RPC_TIMEOUT             (6*HZ)
        
/*      
 * Size of the lookup cache in units of number of entries cached.
 * It is better not to make this too large although the optimum
 * depends on a usage and environment.  
 */     

#define NFS_LOOKUP_CACHE_SIZE           64

#define NFS_SUPER_MAGIC                 0x6969

#define NFS_SERVER(inode)               (&(inode)->i_sb->u.nfs_sb.s_server)
#define NFS_FH(inode)                   (&(inode)->u.nfs_i.fhandle)
#define NFS_RENAMED_DIR(inode)          ((inode)->u.nfs_i.silly_rename_dir)
#define NFS_READTIME(inode)             ((inode)->u.nfs_i.read_cache_jiffies)
#define NFS_OLDMTIME(inode)             ((inode)->u.nfs_i.read_cache_mtime)
#define NFS_ATTRTIMEO(inode)            ((inode)->u.nfs_i.attrtimeo)
#define NFS_MINATTRTIMEO(inode)         (S_ISREG((inode)->i_mode)?      \
                                                NFS_SERVER(inode)->acregmin : \
                                                NFS_SERVER(inode)->acdirmin)
#define NFS_CACHEINV(inode) \
do { \
        NFS_READTIME(inode) = jiffies - 1000000; \
        NFS_OLDMTIME(inode) = 0; \
} while (0)


#define NFS_ROOT                "/tftpboot/%s"
#define NFS_ROOT_NAME_LEN       256
#define NFS_ROOT_ADDRS_LEN      128


#endif
