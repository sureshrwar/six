/*
 * fs/ext4/truncate.c
 *
 * Truncation support for SIX's fs/ext4 driver. Dispatches extent-mapped
 * inodes (EXT4_EXTENTS_FL) to ext4_ext_truncate() in fs/ext4/extents.c.
 */

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ext4_fs.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/locks.h>

void ext4_truncate(struct inode *inode)
{
	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	      S_ISLNK(inode->i_mode)))
		return;
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		return;

	if (inode->u.ext2_i.i_flags & EXT4_EXTENTS_FL) {
		ext4_ext_truncate(inode);
		return;
	}

	/* Fallback for non-extent inodes */
	ext4_discard_prealloc(inode);
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_dirt = 1;
}
