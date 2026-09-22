/*
 * fs/ext4/dir.c
 *
 * Directory reading and entry validation for SIX's fs/ext4 driver,
 * supporting EXT4_FEATURE_INCOMPAT_FILETYPE (8-bit name_len + 8-bit file_type).
 */

#include <asm/segment.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ext4_fs.h>
#include <linux/sched.h>
#include <linux/stat.h>

static int ext4_dir_read(struct inode *inode, struct file *filp,
			 char *buf, int count)
{
	return -EISDIR;
}

static int ext4_readdir(struct inode *, struct file *, void *, filldir_t);

static struct file_operations ext4_dir_operations = {
	NULL,			/* lseek - default */
	ext4_dir_read,		/* read */
	NULL,			/* write - bad */
	ext4_readdir,		/* readdir */
	NULL,			/* select - default */
	ext4_ioctl,		/* ioctl */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* no special release code */
	file_fsync,		/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL			/* revalidate */
};

struct inode_operations ext4_dir_inode_operations = {
	&ext4_dir_operations,	/* default directory file-ops */
	ext4_create,		/* create */
	ext4_lookup,		/* lookup */
	ext4_link,		/* link */
	ext4_unlink,		/* unlink */
	ext4_symlink,		/* symlink */
	ext4_mkdir,		/* mkdir */
	ext4_rmdir,		/* rmdir */
	ext4_mknod,		/* mknod */
	ext4_rename,		/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	ext4_truncate,		/* truncate */
	ext4_permission,	/* permission */
	NULL			/* smap */
};

int ext4_check_dir_entry(const char *function, struct inode *dir,
			 struct ext2_dir_entry *de, struct buffer_head *bh,
			 unsigned long offset)
{
	const char *error_msg = NULL;
	int namelen = EXT4_DIR_NAMELEN(de);

	if (de->rec_len < EXT2_DIR_REC_LEN(1))
		error_msg = "rec_len is smaller than minimal";
	else if (de->rec_len % 4 != 0)
		error_msg = "rec_len % 4 != 0";
	else if (de->rec_len < EXT2_DIR_REC_LEN(namelen))
		error_msg = "rec_len is too small for name_len";
	else if (dir && ((char *)de - bh->b_data) + de->rec_len >
		 dir->i_sb->s_blocksize)
		error_msg = "directory entry across blocks";
	else if (dir && de->inode > dir->i_sb->u.ext2_sb.s_es->s_inodes_count)
		error_msg = "inode out of bounds";

	if (error_msg != NULL)
		ext4_error(dir->i_sb, function,
			   "bad entry in directory #%lu: %s - "
			   "offset=%lu, inode=%lu, rec_len=%d, name_len=%d",
			   dir->i_ino, error_msg, offset,
			   (unsigned long)de->inode, de->rec_len, namelen);
	return error_msg == NULL ? 1 : 0;
}

static int ext4_readdir(struct inode *inode, struct file *filp,
			void *dirent, filldir_t filldir)
{
	int error = 0;
	unsigned long offset, blk;
	int i, num, stored;
	struct buffer_head *bh, *tmp, *bha[16];
	struct ext2_dir_entry *de;
	struct super_block *sb;
	int err;

	if (!inode || !S_ISDIR(inode->i_mode))
		return -EBADF;
	sb = inode->i_sb;

	stored = 0;
	bh = NULL;
	offset = filp->f_pos & (sb->s_blocksize - 1);

	while (!error && !stored && filp->f_pos < inode->i_size) {
		blk = (filp->f_pos) >> EXT2_BLOCK_SIZE_BITS(sb);
		bh = ext4_bread(inode, blk, 0, &err);
		if (!bh) {
			/*
			 * In ext4 dir_index (HTree) directories, internal dx_node
			 * blocks or holes can exist; skip cleanly to next block.
			 */
			filp->f_pos += sb->s_blocksize - offset;
			offset = 0;
			continue;
		}

		if (!offset) {
			for (i = 16 >> (EXT2_BLOCK_SIZE_BITS(sb) - 9), num = 0;
			     i > 0; i--) {
				tmp = ext4_getblk(inode, ++blk, 0, &err);
				if (tmp && !buffer_uptodate(tmp) && !buffer_locked(tmp))
					bha[num++] = tmp;
				else
					brelse(tmp);
			}
			if (num) {
				ll_rw_block(READA, num, bha);
				for (i = 0; i < num; i++)
					brelse(bha[i]);
			}
		}

revalidate:
		if (filp->f_version != inode->i_version) {
			for (i = 0; i < sb->s_blocksize && i < offset; ) {
				de = (struct ext2_dir_entry *)(bh->b_data + i);
				if (de->rec_len < EXT2_DIR_REC_LEN(1))
					break;
				i += de->rec_len;
			}
			offset = i;
			filp->f_pos = (filp->f_pos & ~(sb->s_blocksize - 1)) | offset;
			filp->f_version = inode->i_version;
		}

		while (!error && filp->f_pos < inode->i_size &&
		       offset < sb->s_blocksize) {
			int namelen;
			de = (struct ext2_dir_entry *)(bh->b_data + offset);
			/*
			 * Skip HTree index root / internal node blocks:
			 * In dx_root (block 0 of an indexed dir), after "." (offset 0)
			 * and ".." (offset 12), the ".." entry has rec_len = blocksize - 12
			 * covering the dx_root_info + dx_entry table!
			 * In an internal dx_node block (de->inode == 0 && de->rec_len == blocksize),
			 * de->inode is 0 so it is skipped naturally.
			 */
			if (!ext4_check_dir_entry("ext4_readdir", inode, de, bh, offset)) {
				filp->f_pos = (filp->f_pos & ~(sb->s_blocksize - 1)) +
					      sb->s_blocksize;
				brelse(bh);
				return stored;
			}
			offset += de->rec_len;
			namelen = EXT4_DIR_NAMELEN(de);
			if (de->inode && namelen > 0) {
				unsigned long version;
				dcache_add(inode, de->name, namelen, de->inode);
				version = inode->i_version;
				error = filldir(dirent, de->name, namelen,
						filp->f_pos, de->inode);
				if (error)
					break;
				if (version != inode->i_version)
					goto revalidate;
				stored++;
			}
			filp->f_pos += de->rec_len;
		}
		offset = 0;
		brelse(bh);
	}
	if (!IS_RDONLY(inode)) {
		inode->i_atime = CURRENT_TIME;
		inode->i_dirt = 1;
	}
	return 0;
}
