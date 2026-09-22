/*
 * fs/ext4/fsync.c
 */

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ext4_fs.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/locks.h>

int ext4_sync_file(struct inode *inode, struct file *file)
{
	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	      S_ISLNK(inode->i_mode)))
		return -EINVAL;
	return ext4_sync_inode(inode);
}
