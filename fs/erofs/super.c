/*
 * fs/erofs/super.c
 *
 * Enhanced Read-Only File System (EROFS v1, magic 0xE0F5E1E2) VFS driver
 * for SIX (Linux 2.0.11).
 *
 * Supports:
 *   - EROFS v1 superblock at offset 1024 (EROFS_SUPER_MAGIC_V1)
 *   - Compact (32-byte) and Extended (64-byte) inodes addressed directly
 *     by 64-bit NID: disk_offset = (meta_blkaddr << blkszbits) + (nid << 5)
 *   - EROFS_INODE_FLAT_PLAIN (contiguous block mapping, O(1) bmap + mmap)
 *   - EROFS_INODE_FLAT_INLINE (tail-packing inline data right after inode)
 *   - Sorted EROFS directory blocks (struct erofs_dirent, 12-byte headers)
 *   - Symlinks (readlink + follow_link) and device nodes
 */

#include <linux/module.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/bitops.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/erofs_fs.h>
#include <linux/malloc.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/locks.h>
#include <linux/mm.h>
#include <linux/pagemap.h>

static void erofs_read_inode(struct inode *inode);
static void erofs_put_super(struct super_block *sb);
static void erofs_statfs(struct super_block *sb, struct statfs *buf, int bufsiz);
static int erofs_remount(struct super_block *sb, int *flags, char *data);

static int erofs_bmap(struct inode *inode, int block);
static int erofs_readpage(struct inode *inode, struct page *page);
static int erofs_file_read(struct inode *inode, struct file *filp, char *buf, int count);
static int erofs_readdir(struct inode *inode, struct file *filp, void *dirent, filldir_t filldir);
static int erofs_lookup(struct inode *dir, const char *name, int len, struct inode **result);
static int erofs_readlink(struct inode *inode, char *buffer, int buflen);
static int erofs_follow_link(struct inode *dir, struct inode *inode,
			     int flag, int mode, struct inode **res_inode);

static struct super_operations erofs_sops = {
	erofs_read_inode,
	NULL,			/* notify_change */
	NULL,			/* write_inode */
	NULL,			/* put_inode */
	erofs_put_super,
	NULL,			/* write_super */
	erofs_statfs,
	erofs_remount
};

static struct file_operations erofs_file_operations = {
	NULL,			/* lseek - default */
	erofs_file_read,	/* read */
	NULL,			/* write - read-only fs */
	NULL,			/* readdir */
	NULL,			/* select */
	NULL,			/* ioctl */
	generic_file_mmap,	/* mmap */
	NULL,			/* open */
	NULL,			/* release */
	NULL,			/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL			/* revalidate */
};

static struct inode_operations erofs_file_inode_operations = {
	&erofs_file_operations,
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
	erofs_readpage,		/* readpage */
	NULL,			/* writepage */
	erofs_bmap,		/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL			/* smap */
};

static int erofs_dir_read(struct inode *inode, struct file *filp, char *buf, int count)
{
	return -EISDIR;
}

static struct file_operations erofs_dir_operations = {
	NULL,			/* lseek */
	erofs_dir_read,		/* read */
	NULL,			/* write */
	erofs_readdir,		/* readdir */
	NULL,			/* select */
	NULL,			/* ioctl */
	NULL,			/* mmap */
	NULL,			/* open */
	NULL,			/* release */
	NULL,			/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL			/* revalidate */
};

static struct inode_operations erofs_dir_inode_operations = {
	&erofs_dir_operations,
	NULL,			/* create */
	erofs_lookup,		/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL			/* smap */
};

static struct inode_operations erofs_symlink_inode_operations = {
	NULL,			/* default_file_ops */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	erofs_readlink,		/* readlink */
	erofs_follow_link,	/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL			/* smap */
};

/*
 * Read raw bytes from disk at arbitrary byte offset `disk_off`.
 * Handles reads that cross 1024-byte block boundaries cleanly.
 */
static int erofs_read_disk_bytes(struct super_block *sb, unsigned long disk_off,
				 char *dst, int len)
{
	unsigned long blksz = sb->s_blocksize;
	unsigned char blkszbits = sb->s_blocksize_bits;

	while (len > 0) {
		unsigned long blk = disk_off >> blkszbits;
		unsigned long boff = disk_off & (blksz - 1);
		int chunk = blksz - boff;
		struct buffer_head *bh;

		if (chunk > len)
			chunk = len;
		bh = bread(sb->s_dev, blk, blksz);
		if (!bh)
			return -EIO;
		memcpy(dst, bh->b_data + boff, chunk);
		brelse(bh);

		dst += chunk;
		disk_off += chunk;
		len -= chunk;
	}
	return 0;
}

/*
 * Read `len` bytes from an EROFS inode starting at file byte offset `pos`
 * into kernel buffer `kbuf`. Supports both EROFS_INODE_FLAT_PLAIN and
 * EROFS_INODE_FLAT_INLINE (tail-packing).
 */
static int erofs_read_inode_kbuf(struct inode *inode, unsigned long pos,
				 char *kbuf, int len)
{
	struct super_block *sb = inode->i_sb;
	unsigned long blksz = sb->s_blocksize;
	unsigned char blkszbits = sb->s_blocksize_bits;
	unsigned long full_blocks = inode->i_size >> blkszbits;
	unsigned long datalayout = EROFS_I_DATALAYOUT(inode);
	unsigned long raw_blkaddr = EROFS_I_RAW_BLKADDR(inode);
	unsigned long inline_off = EROFS_I_INLINE_OFF(inode);
	int total = 0;

	if (pos >= inode->i_size || len <= 0)
		return 0;
	if (pos + len > inode->i_size)
		len = inode->i_size - pos;

	while (total < len) {
		unsigned long lblk = pos >> blkszbits;
		unsigned long boff = pos & (blksz - 1);
		int chunk = blksz - boff;
		int err;

		if (chunk > len - total)
			chunk = len - total;

		if (datalayout == EROFS_INODE_FLAT_PLAIN || lblk < full_blocks) {
			unsigned long disk_off = ((raw_blkaddr + lblk) << blkszbits) + boff;
			err = erofs_read_disk_bytes(sb, disk_off, kbuf + total, chunk);
		} else if (datalayout == EROFS_INODE_FLAT_INLINE) {
			unsigned long disk_off = inline_off + boff;
			err = erofs_read_disk_bytes(sb, disk_off, kbuf + total, chunk);
		} else {
			return -EIO;
		}
		if (err)
			return total > 0 ? total : err;

		pos += chunk;
		total += chunk;
	}
	return total;
}

static int erofs_bmap(struct inode *inode, int block)
{
	unsigned long blksz;
	unsigned long nblocks;
	unsigned long datalayout;

	if (!inode || !inode->i_sb || block < 0)
		return 0;
	blksz = inode->i_sb->s_blocksize;
	if ((unsigned long)block * blksz >= inode->i_size)
		return 0;

	datalayout = EROFS_I_DATALAYOUT(inode);
	nblocks = inode->i_size >> inode->i_sb->s_blocksize_bits;
	if (datalayout == EROFS_INODE_FLAT_PLAIN || (unsigned long)block < nblocks)
		return EROFS_I_RAW_BLKADDR(inode) + block;
	return 0;
}

static int erofs_readpage(struct inode *inode, struct page *page)
{
	unsigned long address;
	int nread;

	if (EROFS_I_DATALAYOUT(inode) == EROFS_INODE_FLAT_PLAIN)
		return generic_readpage(inode, page);

	address = page_address(page);
	memset((void *)address, 0, PAGE_SIZE);
	nread = erofs_read_inode_kbuf(inode, page->offset, (char *)address, PAGE_SIZE);
	if (nread < 0)
		return nread;
	set_bit(PG_uptodate, &page->flags);
	return 0;
}

static int erofs_file_read(struct inode *inode, struct file *filp,
			   char *buf, int count)
{
	char ktmp[1024];
	int total = 0;

	if (!inode)
		return -EINVAL;
	if (count <= 0 || filp->f_pos >= inode->i_size)
		return 0;
	if (filp->f_pos + count > inode->i_size)
		count = inode->i_size - filp->f_pos;

	while (total < count) {
		int ask = count - total;
		int got;

		if (ask > (int)sizeof(ktmp))
			ask = sizeof(ktmp);
		got = erofs_read_inode_kbuf(inode, filp->f_pos, ktmp, ask);
		if (got <= 0)
			return total > 0 ? total : got;
		memcpy_tofs(buf + total, ktmp, got);
		filp->f_pos += got;
		total += got;
	}
	return total;
}

/*
 * Read a single directory block (`lblk`) of `dir` into `kbuf` (up to 1024 bytes)
 * and return the number of valid bytes in that block (`<= 1024`).
 */
static int erofs_read_dir_block(struct inode *dir, unsigned long lblk, char *kbuf)
{
	unsigned long blksz = dir->i_sb->s_blocksize;
	unsigned long byte_off = lblk << dir->i_sb->s_blocksize_bits;
	int rem;

	if (byte_off >= dir->i_size)
		return 0;
	rem = dir->i_size - byte_off;
	if (rem > (int)blksz)
		rem = blksz;
	return erofs_read_inode_kbuf(dir, byte_off, kbuf, rem);
}

static int erofs_readdir(struct inode *inode, struct file *filp,
			 void *dirent, filldir_t filldir)
{
	char dblk[1024];
	unsigned long blksz;
	unsigned char blkszbits;

	if (!inode || !S_ISDIR(inode->i_mode))
		return -EBADF;

	blksz = inode->i_sb->s_blocksize;
	blkszbits = inode->i_sb->s_blocksize_bits;

	/*
	 * Encode position in filp->f_pos as:
	 *   high bits (f_pos >> 10): directory block index (lblk)
	 *   low 10 bits (f_pos & 1023): entry index within that directory block
	 */
	while (1) {
		unsigned long lblk = filp->f_pos >> blkszbits;
		unsigned int idx = filp->f_pos & (blksz - 1);
		int blklen;
		struct erofs_dirent *de;
		unsigned int nr_entries;

		if ((lblk << blkszbits) >= inode->i_size)
			break;

		blklen = erofs_read_dir_block(inode, lblk, dblk);
		if (blklen < (int)sizeof(struct erofs_dirent))
			break;

		de = (struct erofs_dirent *)dblk;
		if (de[0].nameoff < sizeof(struct erofs_dirent) ||
		    de[0].nameoff > (unsigned int)blklen)
			break;
		nr_entries = de[0].nameoff / sizeof(struct erofs_dirent);

		while (idx < nr_entries) {
			unsigned int nameoff = de[idx].nameoff;
			unsigned int nextoff = (idx + 1 < nr_entries) ?
					       de[idx + 1].nameoff : (unsigned int)blklen;
			int namelen;
			const char *name;
			unsigned long target_ino;

			if (nameoff >= (unsigned int)blklen || nextoff > (unsigned int)blklen ||
			    nextoff < nameoff) {
				idx++;
				continue;
			}
			name = dblk + nameoff;
			namelen = nextoff - nameoff;
			while (namelen > 0 && name[namelen - 1] == '\0')
				namelen--;

			target_ino = (unsigned long)de[idx].nid;
			if (namelen > 0 && target_ino > 0) {
				off_t cur_pos = (lblk << blkszbits) | idx;
				if (filldir(dirent, name, namelen, cur_pos, target_ino) < 0) {
					filp->f_pos = cur_pos;
					return 0;
				}
			}
			idx++;
			filp->f_pos = (lblk << blkszbits) | idx;
		}
		filp->f_pos = (lblk + 1) << blkszbits;
	}
	return 0;
}

static int erofs_lookup(struct inode *dir, const char *name, int len,
			struct inode **result)
{
	char dblk[1024];
	unsigned long lblk;
	unsigned long total_blocks;

	*result = NULL;
	if (!dir)
		return -ENOENT;
	if (!S_ISDIR(dir->i_mode)) {
		iput(dir);
		return -ENOENT;
	}
	if (len == 0 || (len == 1 && name[0] == '.')) {
		*result = dir;
		return 0;
	}

	total_blocks = (dir->i_size + dir->i_sb->s_blocksize - 1) >>
		       dir->i_sb->s_blocksize_bits;

	for (lblk = 0; lblk < total_blocks; lblk++) {
		int blklen = erofs_read_dir_block(dir, lblk, dblk);
		struct erofs_dirent *de;
		unsigned int nr_entries, idx;

		if (blklen < (int)sizeof(struct erofs_dirent))
			continue;
		de = (struct erofs_dirent *)dblk;
		if (de[0].nameoff < sizeof(struct erofs_dirent) ||
		    de[0].nameoff > (unsigned int)blklen)
			continue;
		nr_entries = de[0].nameoff / sizeof(struct erofs_dirent);

		for (idx = 0; idx < nr_entries; idx++) {
			unsigned int nameoff = de[idx].nameoff;
			unsigned int nextoff = (idx + 1 < nr_entries) ?
					       de[idx + 1].nameoff : (unsigned int)blklen;
			int namelen;
			const char *ename;

			if (nameoff >= (unsigned int)blklen || nextoff > (unsigned int)blklen ||
			    nextoff < nameoff)
				continue;
			ename = dblk + nameoff;
			namelen = nextoff - nameoff;
			while (namelen > 0 && ename[namelen - 1] == '\0')
				namelen--;

			if (namelen == len && memcmp(ename, name, len) == 0) {
				unsigned long ino = (unsigned long)de[idx].nid;
				iput(dir);
				*result = iget(dir->i_sb, ino);
				return (*result) ? 0 : -EACCES;
			}
		}
	}

	iput(dir);
	return -ENOENT;
}

static int erofs_readlink(struct inode *inode, char *buffer, int buflen)
{
	char linkbuf[512];
	int len, i;

	if (!S_ISLNK(inode->i_mode)) {
		iput(inode);
		return -EINVAL;
	}
	if (buflen > (int)sizeof(linkbuf) - 1)
		buflen = sizeof(linkbuf) - 1;
	len = erofs_read_inode_kbuf(inode, 0, linkbuf, buflen);
	if (len < 0) {
		iput(inode);
		return len;
	}
	for (i = 0; i < len && linkbuf[i]; i++)
		put_user(linkbuf[i], buffer++);
	iput(inode);
	return i;
}

static int erofs_follow_link(struct inode *dir, struct inode *inode,
			     int flag, int mode, struct inode **res_inode)
{
	char linkbuf[512];
	int len, error;

	*res_inode = NULL;
	if (!dir) {
		dir = current->fs->root;
		dir->i_count++;
	}
	if (!inode) {
		iput(dir);
		return -ENOENT;
	}
	if (!S_ISLNK(inode->i_mode)) {
		iput(dir);
		*res_inode = inode;
		return 0;
	}
	if (current->link_count > 5) {
		iput(dir);
		iput(inode);
		return -ELOOP;
	}
	len = erofs_read_inode_kbuf(inode, 0, linkbuf, sizeof(linkbuf) - 1);
	if (len < 0) {
		iput(dir);
		iput(inode);
		return len;
	}
	linkbuf[len] = '\0';
	current->link_count++;
	error = open_namei(linkbuf, flag, mode, res_inode, dir);
	current->link_count--;
	iput(inode);
	return error;
}

static void erofs_read_inode(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct erofs_sb_info *sbi = (struct erofs_sb_info *)sb->u.generic_sbp;
	unsigned long nid = inode->i_ino;
	unsigned long inode_off;
	unsigned char raw_buf[64];
	__u16 i_format, xattr_icount;
	unsigned int isize_hdr, xattr_isize, datalayout;
	unsigned long raw_blkaddr = 0;

	inode->i_op = NULL;
	inode->i_mode = 0;
	if (!sbi)
		return;

	inode_off = ((unsigned long)sbi->meta_blkaddr << sbi->blkszbits) +
		    (nid << EROFS_ISLOTBITS);
	if (erofs_read_disk_bytes(sb, inode_off, (char *)raw_buf, sizeof(raw_buf)) != 0) {
		printk("EROFS-fs: unable to read inode nid=%lu at offset %lu\n",
		       nid, inode_off);
		return;
	}

	i_format = *(__u16 *)raw_buf;
	datalayout = (i_format >> EROFS_I_DATALAYOUT_BIT) & EROFS_I_DATALAYOUT_MASK;

	if ((i_format & EROFS_I_VERSION_MASK) == EROFS_I_VERS_COMPACT) {
		struct erofs_inode_compact *ic = (struct erofs_inode_compact *)raw_buf;
		isize_hdr = sizeof(struct erofs_inode_compact); /* 32 */
		xattr_icount = ic->i_xattr_icount;
		inode->i_mode = ic->i_mode;
		inode->i_nlink = ic->i_nlink;
		inode->i_size = ic->i_size;
		raw_blkaddr = ic->i_u;
		inode->i_uid = ic->i_uid;
		inode->i_gid = ic->i_gid;
		inode->i_mtime = inode->i_atime = inode->i_ctime = (unsigned long)sbi->build_time;
	} else {
		struct erofs_inode_extended *ie = (struct erofs_inode_extended *)raw_buf;
		isize_hdr = sizeof(struct erofs_inode_extended); /* 64 */
		xattr_icount = ie->i_xattr_icount;
		inode->i_mode = ie->i_mode;
		inode->i_nlink = ie->i_nlink;
		inode->i_size = (unsigned long)ie->i_size;
		raw_blkaddr = ie->i_u;
		inode->i_uid = ie->i_uid;
		inode->i_gid = ie->i_gid;
		inode->i_mtime = inode->i_atime = inode->i_ctime = (unsigned long)ie->i_mtime;
	}

	xattr_isize = (xattr_icount == 0) ? 0 : (12 + (xattr_icount - 1) * 4);
	inode->i_blksize = sb->s_blocksize;
	inode->i_blocks = (inode->i_size + 511) >> 9;

	EROFS_I_DATALAYOUT(inode) = datalayout;
	EROFS_I_RAW_BLKADDR(inode) = raw_blkaddr;
	EROFS_I_INLINE_OFF(inode) = inode_off + isize_hdr + xattr_isize;
	EROFS_I_NID(inode) = nid;

	if (S_ISREG(inode->i_mode))
		inode->i_op = &erofs_file_inode_operations;
	else if (S_ISDIR(inode->i_mode))
		inode->i_op = &erofs_dir_inode_operations;
	else if (S_ISLNK(inode->i_mode))
		inode->i_op = &erofs_symlink_inode_operations;
	else if (S_ISCHR(inode->i_mode)) {
		inode->i_op = &chrdev_inode_operations;
		inode->i_rdev = to_kdev_t(raw_blkaddr);
	} else if (S_ISBLK(inode->i_mode)) {
		inode->i_op = &blkdev_inode_operations;
		inode->i_rdev = to_kdev_t(raw_blkaddr);
	} else if (S_ISFIFO(inode->i_mode))
		init_fifo(inode);
}

static void erofs_put_super(struct super_block *sb)
{
	struct erofs_sb_info *sbi;

	lock_super(sb);
	sbi = (struct erofs_sb_info *)sb->u.generic_sbp;
	if (sbi) {
		if (sbi->s_sbh)
			brelse(sbi->s_sbh);
		kfree_s(sbi, sizeof(struct erofs_sb_info));
		sb->u.generic_sbp = NULL;
	}
	sb->s_dev = 0;
	unlock_super(sb);
	MOD_DEC_USE_COUNT;
}

static void erofs_statfs(struct super_block *sb, struct statfs *buf, int bufsiz)
{
	struct erofs_sb_info *sbi = (struct erofs_sb_info *)sb->u.generic_sbp;
	struct statfs tmp;

	memset(&tmp, 0, sizeof(tmp));
	tmp.f_type = EROFS_SUPER_MAGIC_V1;
	tmp.f_bsize = sb->s_blocksize;
	tmp.f_blocks = sbi ? sbi->blocks : 0;
	tmp.f_bfree = 0;
	tmp.f_bavail = 0;
	tmp.f_files = sbi ? (long)sbi->inos : 0;
	tmp.f_ffree = 0;
	tmp.f_namelen = EROFS_NAME_LEN;
	memcpy_tofs(buf, &tmp, bufsiz);
}

static int erofs_remount(struct super_block *sb, int *flags, char *data)
{
	if (!(*flags & MS_RDONLY))
		return -EROFS;
	return 0;
}

struct super_block *erofs_read_super(struct super_block *sb, void *data, int silent)
{
	struct buffer_head *bh;
	struct erofs_super_block *esb;
	struct erofs_sb_info *sbi;
	kdev_t dev = sb->s_dev;

	MOD_INC_USE_COUNT;
	lock_super(sb);
	set_blocksize(dev, BLOCK_SIZE);

	if (!(bh = bread(dev, 1, BLOCK_SIZE))) {
		sb->s_dev = 0;
		unlock_super(sb);
		if (!silent)
			printk("EROFS-fs: unable to read superblock on %s\n",
			       kdevname(dev));
		MOD_DEC_USE_COUNT;
		return NULL;
	}

	esb = (struct erofs_super_block *)bh->b_data;
	if (esb->magic != EROFS_SUPER_MAGIC_V1) {
		brelse(bh);
		sb->s_dev = 0;
		unlock_super(sb);
		if (!silent)
			printk("VFS: Can't find an EROFS filesystem on dev %s.\n",
			       kdevname(dev));
		MOD_DEC_USE_COUNT;
		return NULL;
	}

	sbi = (struct erofs_sb_info *)kmalloc(sizeof(struct erofs_sb_info), GFP_KERNEL);
	if (!sbi) {
		brelse(bh);
		sb->s_dev = 0;
		unlock_super(sb);
		MOD_DEC_USE_COUNT;
		return NULL;
	}
	memset(sbi, 0, sizeof(*sbi));
	sbi->s_sbh = bh;
	sbi->meta_blkaddr = esb->meta_blkaddr;
	sbi->blocks = esb->blocks;
	sbi->inos = esb->inos;
	sbi->build_time = esb->build_time;
	sbi->root_nid = esb->root_nid;
	sbi->blkszbits = esb->blkszbits ? esb->blkszbits : EROFS_BLKSZBITS;
	memcpy(sbi->uuid, esb->uuid, 16);
	memcpy(sbi->volume_name, esb->volume_name, 16);
	sbi->volume_name[16] = '\0';

	sb->s_blocksize_bits = sbi->blkszbits;
	sb->s_blocksize = 1UL << sbi->blkszbits;
	sb->s_magic = EROFS_SUPER_MAGIC_V1;
	sb->s_flags |= MS_RDONLY;
	sb->s_rd_only = 1;
	sb->s_dirt = 0;
	sb->u.generic_sbp = sbi;
	sb->s_op = &erofs_sops;

	unlock_super(sb);
	sb->s_mounted = iget(sb, (unsigned long)sbi->root_nid);
	if (!sb->s_mounted) {
		erofs_put_super(sb);
		printk("EROFS-fs: get root inode (nid=%u) failed\n", sbi->root_nid);
		return NULL;
	}

	printk("EROFS-fs (%s): mounted read-only filesystem (blksz=%lu, blocks=%u, inos=%lu, root_nid=%u, label=%s)\n",
	       kdevname(dev), sb->s_blocksize, sbi->blocks,
	       (unsigned long)sbi->inos, sbi->root_nid,
	       sbi->volume_name[0] ? sbi->volume_name : "none");
	return sb;
}

static struct file_system_type erofs_fs_type = {
	erofs_read_super, "erofs", 1, NULL
};

int init_erofs_fs(void)
{
	return register_filesystem(&erofs_fs_type);
}
