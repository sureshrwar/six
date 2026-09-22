/*
 * fs/ext4/acl.c
 */

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ext4_fs.h>
#include <linux/sched.h>
#include <linux/stat.h>

int ext4_permission(struct inode *inode, int mask)
{
	unsigned short mode = inode->i_mode;

	if ((mask & MAY_WRITE) && IS_RDONLY(inode) &&
	    (S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	     S_ISLNK(inode->i_mode)))
		return -EROFS;
	if ((mask & MAY_WRITE) && IS_IMMUTABLE(inode))
		return -EACCES;
	if (current->fsuid == inode->i_uid)
		mode >>= 6;
	else if (in_group_p(inode->i_gid))
		mode >>= 3;
	if (((mode & mask & S_IRWXO) == mask) || fsuser())
		return 0;
	return -EACCES;
}
