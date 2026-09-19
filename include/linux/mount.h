
/*                      
 *
 * Definitions for mount interface. This describes the in the kernel build
 * linkedlist with mounted filesystems. 
 *                      
 * Author:  Marco van Wieringen <mvw@mcs.ow.nl> <mvw@tnix.net> <mvw@cistron.nl>
 *                      
 * Version: $Id: mount.h,v 1.1.1.1 2005/03/30 08:40:49 motorbreathing Exp $
 *                              
 */                     
#ifndef _LINUX_MOUNT_H
#define _LINUX_MOUNT_H
        
struct vfsmount 
{                       
   kdev_t mnt_dev;                     /* Device this applies to */
   char *mnt_devname;                  /* Name of device e.g. /dev/dsk/hda1 */
   char *mnt_dirname;                  /* Name of directory mounted on */
   unsigned int mnt_flags;             /* Flags of this device */
   struct semaphore mnt_sem;           /* lock device while I/O in progress */
   struct super_block *mnt_sb;         /* pointer to superblock */
   struct file *mnt_quotas[MAXQUOTAS]; /* fp's to quotafiles */
   time_t mnt_iexp[MAXQUOTAS];         /* expiretime for inodes */
   time_t mnt_bexp[MAXQUOTAS];         /* expiretime for blocks */
   struct vfsmount *mnt_next;          /* pointer to next in linkedlist */
};      
        
        
#endif /* _LINUX_MOUNT_H */

