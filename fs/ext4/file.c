/*
 * fs/ext4/file.c
 *
 * Regular file read/write operations for SIX's fs/ext4 driver.
 */

#include <asm/segment.h>
#include <asm/system.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ext4_fs.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/locks.h>
#include <linux/mm.h>
#include <linux/pagemap.h>

#define NBUF 32
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

static int ext4_file_write(struct inode *inode, struct file *filp,
			   const char *buf, int count);

static void ext4_release_file(struct inode *inode, struct file *filp)
{
	if (filp->f_mode & 2)
		ext4_discard_prealloc(inode);
}

static struct file_operations ext4_file_operations = {
	NULL,			/* lseek - default */
	generic_file_read,	/* read */
	ext4_file_write,	/* write */
	NULL,			/* readdir - bad */
	NULL,			/* select - default */
	ext4_ioctl,		/* ioctl */
	generic_file_mmap,	/* mmap */
	NULL,			/* no special open is needed */
	ext4_release_file,	/* release */
	ext4_sync_file,		/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL			/* revalidate */
};

struct inode_operations ext4_file_inode_operations = {
	&ext4_file_operations,	/* default file operations */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	generic_readpage,	/* readpage */
	NULL,			/* writepage */
	ext4_bmap,		/* bmap */
	ext4_truncate,		/* truncate */
	ext4_permission,	/* permission */
	NULL			/* smap */
};

static int ext4_file_write(struct inode *inode, struct file *filp,
			   const char *buf, int count)
{
	off_t pos;
	int written, c;
	struct buffer_head *bh, *bufferlist[NBUF];
	char *p;
	struct super_block *sb;
	int err;
	int i, buffercount, write_error;

	write_error = buffercount = 0;
	if (!inode) {
		printk("ext4_file_write: inode = NULL\n");
		return -EINVAL;
	}
	sb = inode->i_sb;
	if (sb->s_flags & MS_RDONLY)
		return -EROFS;
	if (!S_ISREG(inode->i_mode))
		return -EINVAL;
	/*
	 * NOTE: do *not* take inode->i_sem here.  In 2.0.x the VFS already
	 * holds it across the ->write() call (see sys_write() in
	 * fs/read_write.c), and these semaphores are not recursive, so a
	 * second down() would deadlock the caller.  fs/ext2/file.c likewise
	 * relies on the caller's lock.
	 */
	if (filp->f_flags & O_APPEND)
		pos = inode->i_size;
	else
		pos = filp->f_pos;
	written = 0;
	while (written < count) {
		bh = ext4_getblk(inode, pos >> EXT2_BLOCK_SIZE_BITS(sb), 1, &err);
		if (!bh) {
			if (!written)
				written = err;
			break;
		}
		c = sb->s_blocksize - (pos & (sb->s_blocksize - 1));
		if (c > count - written)
			c = count - written;
		if (c != sb->s_blocksize && !buffer_uptodate(bh)) {
			ll_rw_block(READ, 1, &bh);
			wait_on_buffer(bh);
			if (!buffer_uptodate(bh)) {
				brelse(bh);
				if (!written)
					written = -EIO;
				break;
			}
		}
		p = (pos & (sb->s_blocksize - 1)) + bh->b_data;
		memcpy_fromfs(p, buf, c);
		update_vm_cache(inode, pos, p, c);
		pos += c;
		if (pos > inode->i_size) {
			inode->i_size = pos;
			inode->i_dirt = 1;
		}
		written += c;
		buf += c;
		mark_buffer_uptodate(bh, 1);
		mark_buffer_dirty(bh, 0);
		if (filp->f_flags & O_SYNC)
			bufferlist[buffercount++] = bh;
		else
			brelse(bh);
		if (buffercount == NBUF) {
			ll_rw_block(WRITE, buffercount, bufferlist);
			for (i = 0; i < buffercount; i++) {
				wait_on_buffer(bufferlist[i]);
				if (!buffer_uptodate(bufferlist[i]))
					write_error = 1;
				brelse(bufferlist[i]);
			}
			buffercount = 0;
		}
		if (write_error)
			break;
	}
	if (buffercount) {
		ll_rw_block(WRITE, buffercount, bufferlist);
		for (i = 0; i < buffercount; i++) {
			wait_on_buffer(bufferlist[i]);
			if (!buffer_uptodate(bufferlist[i]))
				write_error = 1;
			brelse(bufferlist[i]);
		}
	}
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	filp->f_pos = pos;
	inode->i_dirt = 1;
	if (filp->f_flags & O_SYNC) {
		err = ext4_sync_inode(inode);
		if (err)
			write_error = 1;
	}
	if (write_error)
		return -EIO;
	return written;
}
