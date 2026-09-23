/*
 * linux/fs/overlay/inode.c
 *
 * OverlayFS union/stacking VFS filesystem for SIX (Linux 2.0.11).
 *
 * Layers a writable upper directory (e.g. /var/overlay/bin on /dev/hda)
 * on top of a read-only lower directory (e.g. /bin on /dev/mapper/verity_bin
 * dm-verity).
 *
 * Features:
 *   - Merged directory listing (ovl_readdir) deduplicating upper + lower
 *     entries and hiding whiteout markers (.wh.<name>).
 *   - Zero-copy reads from lowerdir (dm-verity SHA-256 verification stays
 *     active for all unmodified files).
 *   - Automatic Copy-Up (ovl_copy_up) on write/open(O_WRONLY|O_RDWR|O_TRUNC)/
 *     truncate/chmod so modifications are transparently written to upperdir.
 *   - Whiteout creation (.wh.<name>) in upperdir when deleting a file that
 *     exists in lowerdir, and automatic whiteout removal on recreate.
 *   - Live synchronization with upperdir so direct operations on
 *     /var/overlay/bin (such as cp or rm) are immediately reflected in /bin.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/pagemap.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/locks.h>
#include <asm/segment.h>

#define OVERLAYFS_SUPER_MAGIC	0x794c7630UL
#define OVL_ROOT_INO		1UL
#define OVL_NAME_LEN		255
#define OVL_PATH_LEN		128
#define OVL_WHITEOUT_PREFIX	".wh."
#define OVL_WHITEOUT_PREFIX_LEN	4

struct ovl_node {
	unsigned long ino;
	unsigned long parent_ino;
	int namelen;
	char name[OVL_NAME_LEN + 1];
	struct inode *upper_inode;
	struct inode *lower_inode;
	struct ovl_node *next;
};

struct ovl_sb {
	struct inode *lower_root;
	struct inode *upper_root;
	char lowerdir[OVL_PATH_LEN];
	char upperdir[OVL_PATH_LEN];
	unsigned long next_ino;
	struct ovl_node *nodes;
};

extern struct inode_operations ovl_file_inode_operations;
extern struct inode_operations ovl_dir_inode_operations;
extern struct inode_operations ovl_symlink_inode_operations;

static struct ovl_sb *ovl_get_sb(struct super_block *sb)
{
	return sb ? (struct ovl_sb *)sb->u.generic_sbp : NULL;
}

static struct ovl_node *ovl_find_node(struct super_block *sb, unsigned long ino)
{
	struct ovl_sb *osb = ovl_get_sb(sb);
	struct ovl_node *n;

	if (!osb)
		return NULL;
	for (n = osb->nodes; n; n = n->next) {
		if (n->ino == ino)
			return n;
	}
	return NULL;
}

static struct ovl_node *ovl_find_child_node(struct super_block *sb,
					    unsigned long parent_ino,
					    const char *name, int len)
{
	struct ovl_sb *osb = ovl_get_sb(sb);
	struct ovl_node *n;

	if (!osb)
		return NULL;
	for (n = osb->nodes; n; n = n->next) {
		if (n->parent_ino == parent_ino &&
		    n->namelen == len &&
		    memcmp(n->name, name, len) == 0)
			return n;
	}
	return NULL;
}

static int ovl_is_whiteout_name(const char *name, int len)
{
	return (len > OVL_WHITEOUT_PREFIX_LEN &&
		memcmp(name, OVL_WHITEOUT_PREFIX, OVL_WHITEOUT_PREFIX_LEN) == 0);
}

static int ovl_make_whiteout_name(const char *name, int len,
				  char *buf, int bufsize)
{
	if (len <= 0 || len + OVL_WHITEOUT_PREFIX_LEN >= bufsize)
		return -ENAMETOOLONG;
	memcpy(buf, OVL_WHITEOUT_PREFIX, OVL_WHITEOUT_PREFIX_LEN);
	memcpy(buf + OVL_WHITEOUT_PREFIX_LEN, name, len);
	buf[OVL_WHITEOUT_PREFIX_LEN + len] = '\0';
	return OVL_WHITEOUT_PREFIX_LEN + len;
}

/*
 * Helper wrappers around underlying directory i_op methods.
 * Remember: in Linux 2.0.11 VFS, dir->i_op->lookup/create/unlink/mkdir/etc.
 * ALWAYS consume one reference on `dir` via iput(dir)!
 */
static int ovl_real_lookup(struct inode *dir, const char *name, int len,
			   struct inode **res)
{
	*res = NULL;
	if (!dir || !dir->i_op || !dir->i_op->lookup)
		return -ENOENT;
	dir->i_count++;
	return dir->i_op->lookup(dir, name, len, res);
}

static int ovl_real_create(struct inode *dir, const char *name, int len,
			   int mode, struct inode **res)
{
	*res = NULL;
	if (!dir || !dir->i_op || !dir->i_op->create)
		return -EROFS;
	dir->i_count++;
	return dir->i_op->create(dir, name, len, mode, res);
}

static int ovl_real_unlink(struct inode *dir, const char *name, int len)
{
	if (!dir || !dir->i_op || !dir->i_op->unlink)
		return -EROFS;
	dir->i_count++;
	return dir->i_op->unlink(dir, name, len);
}

static int ovl_real_mkdir(struct inode *dir, const char *name, int len, int mode)
{
	if (!dir || !dir->i_op || !dir->i_op->mkdir)
		return -EROFS;
	dir->i_count++;
	return dir->i_op->mkdir(dir, name, len, mode);
}

static int ovl_real_rmdir(struct inode *dir, const char *name, int len)
{
	if (!dir || !dir->i_op || !dir->i_op->rmdir)
		return -EROFS;
	dir->i_count++;
	return dir->i_op->rmdir(dir, name, len);
}

static int ovl_has_whiteout(struct inode *upper_dir, const char *name, int len)
{
	char wh_name[OVL_NAME_LEN + 8];
	int wh_len;
	struct inode *wh_inode = NULL;

	if (!upper_dir)
		return 0;
	wh_len = ovl_make_whiteout_name(name, len, wh_name, sizeof(wh_name));
	if (wh_len < 0)
		return 0;
	if (ovl_real_lookup(upper_dir, wh_name, wh_len, &wh_inode) == 0 &&
	    wh_inode != NULL) {
		iput(wh_inode);
		return 1;
	}
	return 0;
}

static void ovl_remove_whiteout(struct inode *upper_dir, const char *name, int len)
{
	char wh_name[OVL_NAME_LEN + 8];
	int wh_len;
	struct inode *wh_inode = NULL;

	if (!upper_dir)
		return;
	wh_len = ovl_make_whiteout_name(name, len, wh_name, sizeof(wh_name));
	if (wh_len < 0)
		return;
	if (ovl_real_lookup(upper_dir, wh_name, wh_len, &wh_inode) == 0 &&
	    wh_inode != NULL) {
		iput(wh_inode);
		ovl_real_unlink(upper_dir, wh_name, wh_len);
	}
}

static int ovl_create_whiteout(struct inode *upper_dir, const char *name, int len)
{
	char wh_name[OVL_NAME_LEN + 8];
	int wh_len, err;
	struct inode *wh_inode = NULL;

	if (!upper_dir)
		return -EROFS;
	wh_len = ovl_make_whiteout_name(name, len, wh_name, sizeof(wh_name));
	if (wh_len < 0)
		return wh_len;
	if (ovl_real_lookup(upper_dir, wh_name, wh_len, &wh_inode) == 0 &&
	    wh_inode != NULL) {
		iput(wh_inode);
		return 0;
	}
	err = ovl_real_create(upper_dir, wh_name, wh_len, S_IFREG | 0000, &wh_inode);
	if (err == 0 && wh_inode)
		iput(wh_inode);
	return err;
}

static struct inode *ovl_active_inode(struct ovl_node *node)
{
	if (!node)
		return NULL;
	return node->upper_inode ? node->upper_inode : node->lower_inode;
}

static void ovl_sync_inode_attrs(struct inode *inode, struct ovl_node *node)
{
	struct inode *real;

	if (!inode || !node)
		return;
	real = ovl_active_inode(node);
	if (!real)
		return;

	inode->i_mode = real->i_mode;
	inode->i_uid = real->i_uid;
	inode->i_gid = real->i_gid;
	inode->i_nlink = real->i_nlink;
	inode->i_size = real->i_size;
	inode->i_atime = real->i_atime;
	inode->i_mtime = real->i_mtime;
	inode->i_ctime = real->i_ctime;
	inode->i_blksize = real->i_blksize ? real->i_blksize : 4096;
	inode->i_blocks = real->i_blocks;
	inode->i_rdev = real->i_rdev;

	if (S_ISDIR(inode->i_mode))
		inode->i_op = &ovl_dir_inode_operations;
	else if (S_ISLNK(inode->i_mode))
		inode->i_op = &ovl_symlink_inode_operations;
	else
		inode->i_op = &ovl_file_inode_operations;
}

/*
 * Re-synchronize `node->upper_inode` with `parent->upper_inode` on disk so that
 * out-of-band modifications directly inside `/var/overlay/bin` (such as
 * `cp /bin/configure_dm /var/overlay/bin/configure_dm` or `rm /var/overlay/bin/foo`)
 * take effect immediately on `/bin`.
 */
static void ovl_refresh_node(struct super_block *sb, struct ovl_node *node,
			     struct inode *ovl_inode)
{
	struct ovl_node *parent;
	struct inode *fresh_upper = NULL;

	if (!sb || !node || node->ino == OVL_ROOT_INO)
		return;
	parent = ovl_find_node(sb, node->parent_ino);
	if (!parent || !parent->upper_inode)
		return;

	if (ovl_real_lookup(parent->upper_inode, node->name, node->namelen,
			    &fresh_upper) == 0 && fresh_upper != NULL) {
		if (node->upper_inode != fresh_upper) {
			if (node->upper_inode)
				iput(node->upper_inode);
			node->upper_inode = fresh_upper;
		} else {
			iput(fresh_upper);
		}
	} else {
		if (node->upper_inode) {
			iput(node->upper_inode);
			node->upper_inode = NULL;
		}
	}
	if (ovl_inode)
		ovl_sync_inode_attrs(ovl_inode, node);
}

/*
 * Ensure that a directory node has a writable `upper_inode` directory
 * in the upper layer before creating or copying up files inside it.
 */
static int ovl_ensure_upper_dir(struct super_block *sb, struct ovl_node *dir_node)
{
	struct ovl_node *parent;
	struct inode *u_dir = NULL;
	int err;
	umode_t mode;

	if (!dir_node)
		return -ENOENT;
	if (dir_node->upper_inode)
		return 0;
	if (dir_node->ino == OVL_ROOT_INO)
		return -EROFS;

	parent = ovl_find_node(sb, dir_node->parent_ino);
	if (!parent)
		return -ENOENT;
	err = ovl_ensure_upper_dir(sb, parent);
	if (err)
		return err;

	ovl_remove_whiteout(parent->upper_inode, dir_node->name, dir_node->namelen);
	mode = dir_node->lower_inode ? (dir_node->lower_inode->i_mode & 07777) : 0755;
	err = ovl_real_mkdir(parent->upper_inode, dir_node->name, dir_node->namelen, mode);
	if (err && err != -EEXIST)
		return err;
	err = ovl_real_lookup(parent->upper_inode, dir_node->name, dir_node->namelen, &u_dir);
	if (err || !u_dir)
		return err ? err : -ENOENT;
	dir_node->upper_inode = u_dir;
	return 0;
}

/*
 * Perform Copy-Up of a regular file from lowerdir (dm-verity) to upperdir (/dev/hda).
 */
static int ovl_copy_up(struct inode *ovl_inode)
{
	struct super_block *sb;
	struct ovl_node *node, *parent;
	struct inode *lower, *upper = NULL;
	struct file lower_file, upper_file;
	char *buf = NULL;
	unsigned long old_fs;
	off_t pos = 0, remaining;
	int err;

	if (!ovl_inode || !ovl_inode->i_sb)
		return -EINVAL;
	sb = ovl_inode->i_sb;
	if (sb->s_flags & MS_RDONLY)
		return -EROFS;

	node = ovl_find_node(sb, ovl_inode->i_ino);
	if (!node)
		return -ENOENT;
	ovl_refresh_node(sb, node, ovl_inode);
	if (node->upper_inode)
		return 0;
	lower = node->lower_inode;
	if (!lower)
		return -ENOENT;
	if (S_ISDIR(lower->i_mode))
		return ovl_ensure_upper_dir(sb, node);
	if (!S_ISREG(lower->i_mode))
		return -EPERM;

	parent = ovl_find_node(sb, node->parent_ino);
	if (!parent)
		return -ENOENT;
	err = ovl_ensure_upper_dir(sb, parent);
	if (err)
		return err;

	ovl_remove_whiteout(parent->upper_inode, node->name, node->namelen);

	/* If upper file already exists, unlink it first for a clean copy-up */
	if (ovl_real_lookup(parent->upper_inode, node->name, node->namelen, &upper) == 0 &&
	    upper != NULL) {
		iput(upper);
		upper = NULL;
		ovl_real_unlink(parent->upper_inode, node->name, node->namelen);
	}

	err = ovl_real_create(parent->upper_inode, node->name, node->namelen,
			      lower->i_mode, &upper);
	if (err || !upper)
		return err ? err : -EIO;

	/* Copy file data from lower (dm-verity) to upper (/dev/hda) */
	remaining = lower->i_size;
	if (remaining > 0) {
		if (!lower->i_op || !lower->i_op->default_file_ops ||
		    !lower->i_op->default_file_ops->read ||
		    !upper->i_op || !upper->i_op->default_file_ops ||
		    !upper->i_op->default_file_ops->write) {
			iput(upper);
			ovl_real_unlink(parent->upper_inode, node->name, node->namelen);
			return -EIO;
		}
		buf = (char *)get_free_page(GFP_KERNEL);
		if (!buf) {
			iput(upper);
			ovl_real_unlink(parent->upper_inode, node->name, node->namelen);
			return -ENOMEM;
		}

		memset(&lower_file, 0, sizeof(lower_file));
		lower_file.f_inode = lower;
		lower_file.f_mode = 1;
		lower_file.f_flags = O_RDONLY;
		lower_file.f_op = lower->i_op->default_file_ops;

		memset(&upper_file, 0, sizeof(upper_file));
		upper_file.f_inode = upper;
		upper_file.f_mode = 2;
		upper_file.f_flags = O_WRONLY;
		upper_file.f_op = upper->i_op->default_file_ops;

		old_fs = get_fs();
		set_fs(get_ds());
		while (remaining > 0) {
			int chunk = (remaining > PAGE_SIZE) ? PAGE_SIZE : (int)remaining;
			int nread, nwritten;

			lower_file.f_pos = pos;
			nread = lower_file.f_op->read(lower, &lower_file, buf, chunk);
			if (nread <= 0) {
				err = (nread < 0) ? nread : -EIO;
				break;
			}
			upper_file.f_pos = pos;
			nwritten = upper_file.f_op->write(upper, &upper_file, buf, nread);
			if (nwritten != nread) {
				err = (nwritten < 0) ? nwritten : -ENOSPC;
				break;
			}
			pos += nread;
			remaining -= nread;
		}
		set_fs(old_fs);
		free_page((unsigned long)buf);

		if (err) {
			iput(upper);
			ovl_real_unlink(parent->upper_inode, node->name, node->namelen);
			return err;
		}
	}

	upper->i_mode = lower->i_mode;
	upper->i_uid = lower->i_uid;
	upper->i_gid = lower->i_gid;
	upper->i_atime = lower->i_atime;
	upper->i_mtime = lower->i_mtime;
	upper->i_dirt = 1;

	node->upper_inode = upper;
	ovl_sync_inode_attrs(ovl_inode, node);
	return 0;
}

/*
 * Regular file operations
 */
static int ovl_file_lseek(struct inode *inode, struct file *file,
			  off_t offset, int origin)
{
	long long tmp;

	switch (origin) {
	case 0:
		tmp = offset;
		break;
	case 1:
		tmp = file->f_pos + offset;
		break;
	case 2:
		if (!inode)
			return -EINVAL;
		tmp = inode->i_size + offset;
		break;
	default:
		return -EINVAL;
	}
	if (tmp < 0)
		return -EINVAL;
	file->f_pos = tmp;
	file->f_reada = 0;
	return file->f_pos;
}

static int ovl_file_open(struct inode *inode, struct file *filp)
{
	struct ovl_node *node;
	struct inode *real;
	int err;

	if (!inode || !inode->i_sb)
		return -EINVAL;
	node = ovl_find_node(inode->i_sb, inode->i_ino);
	if (!node)
		return -ENOENT;

	ovl_refresh_node(inode->i_sb, node, inode);
	if (ovl_has_whiteout(
		    ovl_find_node(inode->i_sb, node->parent_ino) ?
		    ovl_find_node(inode->i_sb, node->parent_ino)->upper_inode : NULL,
		    node->name, node->namelen))
		return -ENOENT;

	/* Trigger copy-up if opened for write or truncate */
	if ((filp->f_mode & 2) ||
	    (filp->f_flags & (O_WRONLY | O_RDWR | O_TRUNC | O_APPEND))) {
		err = ovl_copy_up(inode);
		if (err)
			return err;
		if ((filp->f_flags & O_TRUNC) && node->upper_inode) {
			node->upper_inode->i_size = 0;
			if (node->upper_inode->i_op &&
			    node->upper_inode->i_op->truncate)
				node->upper_inode->i_op->truncate(node->upper_inode);
			node->upper_inode->i_dirt = 1;
			ovl_sync_inode_attrs(inode, node);
		}
	}

	real = ovl_active_inode(node);
	if (!real)
		return -ENOENT;
	ovl_sync_inode_attrs(inode, node);
	return 0;
}

static int ovl_file_read(struct inode *inode, struct file *filp,
			 char *buf, int count)
{
	struct ovl_node *node;
	struct inode *real;
	struct file real_file;
	int ret;

	if (!inode || !inode->i_sb)
		return -EINVAL;
	node = ovl_find_node(inode->i_sb, inode->i_ino);
	if (!node)
		return -ENOENT;
	ovl_refresh_node(inode->i_sb, node, inode);
	real = ovl_active_inode(node);
	if (!real || !real->i_op || !real->i_op->default_file_ops ||
	    !real->i_op->default_file_ops->read)
		return -EINVAL;

	memset(&real_file, 0, sizeof(real_file));
	real_file.f_inode = real;
	real_file.f_pos = filp->f_pos;
	real_file.f_mode = filp->f_mode;
	real_file.f_flags = filp->f_flags;
	real_file.f_op = real->i_op->default_file_ops;

	ret = real_file.f_op->read(real, &real_file, buf, count);
	if (ret > 0)
		filp->f_pos = real_file.f_pos;
	ovl_sync_inode_attrs(inode, node);
	return ret;
}

static int ovl_file_write(struct inode *inode, struct file *filp,
			  const char *buf, int count)
{
	struct ovl_node *node;
	struct inode *real;
	struct file real_file;
	int err, ret;

	if (!inode || !inode->i_sb)
		return -EINVAL;
	err = ovl_copy_up(inode);
	if (err)
		return err;

	node = ovl_find_node(inode->i_sb, inode->i_ino);
	if (!node || !node->upper_inode)
		return -EROFS;
	real = node->upper_inode;
	if (!real->i_op || !real->i_op->default_file_ops ||
	    !real->i_op->default_file_ops->write)
		return -EINVAL;

	memset(&real_file, 0, sizeof(real_file));
	real_file.f_inode = real;
	real_file.f_pos = (filp->f_flags & O_APPEND) ? real->i_size : filp->f_pos;
	real_file.f_mode = filp->f_mode | 2;
	real_file.f_flags = filp->f_flags;
	real_file.f_op = real->i_op->default_file_ops;

	ret = real_file.f_op->write(real, &real_file, buf, count);
	if (ret > 0)
		filp->f_pos = real_file.f_pos;
	ovl_sync_inode_attrs(inode, node);
	return ret;
}

static int ovl_readpage(struct inode *inode, struct page *page)
{
	struct ovl_node *node;
	struct inode *real;

	if (!inode || !inode->i_sb)
		return -EINVAL;
	node = ovl_find_node(inode->i_sb, inode->i_ino);
	if (!node)
		return -ENOENT;
	ovl_refresh_node(inode->i_sb, node, inode);
	real = ovl_active_inode(node);
	if (real && real->i_op && real->i_op->readpage)
		return real->i_op->readpage(real, page);
	return generic_readpage(inode, page);
}

static int ovl_bmap(struct inode *inode, int block)
{
	struct ovl_node *node;
	struct inode *real;

	if (!inode || !inode->i_sb)
		return 0;
	node = ovl_find_node(inode->i_sb, inode->i_ino);
	if (!node)
		return 0;
	real = ovl_active_inode(node);
	if (real && real->i_op && real->i_op->bmap)
		return real->i_op->bmap(real, block);
	return 0;
}

static void ovl_truncate(struct inode *inode)
{
	struct ovl_node *node;

	if (!inode || !inode->i_sb)
		return;
	if (ovl_copy_up(inode) != 0)
		return;
	node = ovl_find_node(inode->i_sb, inode->i_ino);
	if (node && node->upper_inode) {
		node->upper_inode->i_size = inode->i_size;
		if (node->upper_inode->i_op && node->upper_inode->i_op->truncate)
			node->upper_inode->i_op->truncate(node->upper_inode);
		node->upper_inode->i_dirt = 1;
		ovl_sync_inode_attrs(inode, node);
	}
}

static struct file_operations ovl_file_operations = {
	ovl_file_lseek,		/* lseek */
	ovl_file_read,		/* read */
	ovl_file_write,		/* write */
	NULL,			/* readdir */
	NULL,			/* select */
	NULL,			/* ioctl */
	generic_file_mmap,	/* mmap */
	ovl_file_open,		/* open */
	NULL,			/* release */
	file_fsync,		/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL			/* revalidate */
};

struct inode_operations ovl_file_inode_operations = {
	&ovl_file_operations,	/* default file-ops */
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
	ovl_readpage,		/* readpage */
	NULL,			/* writepage */
	ovl_bmap,		/* bmap */
	ovl_truncate,		/* truncate */
	NULL,			/* permission */
	NULL			/* smap */
};

/*
 * Merged Directory Operations (readdir, lookup, create, unlink, mkdir, rmdir, rename)
 */
#define OVL_MAX_DIR_ENTRIES	256

struct ovl_dir_item {
	char name[OVL_NAME_LEN + 1];
	int namelen;
	unsigned long ino;
	int is_whiteout;
};

struct ovl_readdir_ctx {
	struct ovl_dir_item *items;
	int count;
	int max_items;
	int scanning_upper;
};

static int ovl_collect_filldir(void *buf, const char *name, int namelen,
			       off_t offset, ino_t ino)
{
	struct ovl_readdir_ctx *ctx = (struct ovl_readdir_ctx *)buf;
	int i;

	if (namelen <= 0 || namelen > OVL_NAME_LEN)
		return 0;
	if ((namelen == 1 && name[0] == '.') ||
	    (namelen == 2 && name[0] == '.' && name[1] == '.'))
		return 0;

	if (ctx->scanning_upper && ovl_is_whiteout_name(name, namelen)) {
		const char *orig = name + OVL_WHITEOUT_PREFIX_LEN;
		int orig_len = namelen - OVL_WHITEOUT_PREFIX_LEN;

		for (i = 0; i < ctx->count; i++) {
			if (ctx->items[i].namelen == orig_len &&
			    memcmp(ctx->items[i].name, orig, orig_len) == 0)
				return 0;
		}
		if (ctx->count < ctx->max_items) {
			memcpy(ctx->items[ctx->count].name, orig, orig_len);
			ctx->items[ctx->count].name[orig_len] = '\0';
			ctx->items[ctx->count].namelen = orig_len;
			ctx->items[ctx->count].ino = 0;
			ctx->items[ctx->count].is_whiteout = 1;
			ctx->count++;
		}
		return 0;
	}

	/* Ignore any whiteout files if encountered */
	if (ovl_is_whiteout_name(name, namelen))
		return 0;

	/* Deduplicate against already seen upper entries or whiteouts */
	for (i = 0; i < ctx->count; i++) {
		if (ctx->items[i].namelen == namelen &&
		    memcmp(ctx->items[i].name, name, namelen) == 0)
			return 0;
	}

	if (ctx->count < ctx->max_items) {
		memcpy(ctx->items[ctx->count].name, name, namelen);
		ctx->items[ctx->count].name[namelen] = '\0';
		ctx->items[ctx->count].namelen = namelen;
		ctx->items[ctx->count].ino = ino ? ino : (ctx->count + 10);
		ctx->items[ctx->count].is_whiteout = 0;
		ctx->count++;
	}
	return 0;
}

static int ovl_readdir(struct inode *inode, struct file *filp,
		       void *dirent, filldir_t filldir)
{
	struct ovl_node *node;
	struct ovl_dir_item *items;
	struct ovl_readdir_ctx ctx;
	struct file sub_file;
	int i, visible_idx;

	if (!inode || !S_ISDIR(inode->i_mode))
		return -ENOTDIR;
	node = ovl_find_node(inode->i_sb, inode->i_ino);
	if (!node)
		return -ENOENT;

	ovl_refresh_node(inode->i_sb, node, inode);

	if (filp->f_pos == 0) {
		if (filldir(dirent, ".", 1, 0, inode->i_ino) < 0)
			return 0;
		filp->f_pos = 1;
	}
	if (filp->f_pos == 1) {
		if (filldir(dirent, "..", 2, 1, node->parent_ino) < 0)
			return 0;
		filp->f_pos = 2;
	}

	items = (struct ovl_dir_item *)kmalloc(
		sizeof(struct ovl_dir_item) * OVL_MAX_DIR_ENTRIES, GFP_KERNEL);
	if (!items)
		return -ENOMEM;

	ctx.items = items;
	ctx.count = 0;
	ctx.max_items = OVL_MAX_DIR_ENTRIES;

	/* 1. Scan upperdir first (records upper files + .wh.<name> whiteouts) */
	if (node->upper_inode && node->upper_inode->i_op &&
	    node->upper_inode->i_op->default_file_ops &&
	    node->upper_inode->i_op->default_file_ops->readdir) {
		ctx.scanning_upper = 1;
		memset(&sub_file, 0, sizeof(sub_file));
		sub_file.f_inode = node->upper_inode;
		sub_file.f_pos = 0;
		sub_file.f_op = node->upper_inode->i_op->default_file_ops;
		sub_file.f_op->readdir(node->upper_inode, &sub_file, &ctx,
				       ovl_collect_filldir);
	}

	/* 2. Scan lowerdir next (skips anything in upperdir or whited-out) */
	if (node->lower_inode && node->lower_inode->i_op &&
	    node->lower_inode->i_op->default_file_ops &&
	    node->lower_inode->i_op->default_file_ops->readdir) {
		ctx.scanning_upper = 0;
		memset(&sub_file, 0, sizeof(sub_file));
		sub_file.f_inode = node->lower_inode;
		sub_file.f_pos = 0;
		sub_file.f_op = node->lower_inode->i_op->default_file_ops;
		sub_file.f_op->readdir(node->lower_inode, &sub_file, &ctx,
				       ovl_collect_filldir);
	}

	visible_idx = 2;
	for (i = 0; i < ctx.count; i++) {
		if (items[i].is_whiteout)
			continue;
		if (visible_idx >= filp->f_pos) {
			if (filldir(dirent, items[i].name, items[i].namelen,
				    visible_idx, items[i].ino) < 0)
				break;
			filp->f_pos = visible_idx + 1;
		}
		visible_idx++;
	}

	kfree(items);
	return 0;
}

static struct ovl_node *ovl_get_or_create_child_node(struct super_block *sb,
						     unsigned long parent_ino,
						     const char *name, int len,
						     struct inode *u_inode,
						     struct inode *l_inode)
{
	struct ovl_sb *osb = ovl_get_sb(sb);
	struct ovl_node *n;

	if (!osb)
		return NULL;
	n = ovl_find_child_node(sb, parent_ino, name, len);
	if (n) {
		if (n->upper_inode != u_inode) {
			if (n->upper_inode)
				iput(n->upper_inode);
			n->upper_inode = u_inode;
		} else if (u_inode) {
			iput(u_inode);
		}
		if (n->lower_inode != l_inode) {
			if (n->lower_inode)
				iput(n->lower_inode);
			n->lower_inode = l_inode;
		} else if (l_inode) {
			iput(l_inode);
		}
		return n;
	}

	n = (struct ovl_node *)kmalloc(sizeof(struct ovl_node), GFP_KERNEL);
	if (!n) {
		if (u_inode)
			iput(u_inode);
		if (l_inode)
			iput(l_inode);
		return NULL;
	}
	memset(n, 0, sizeof(*n));
	n->ino = osb->next_ino++;
	n->parent_ino = parent_ino;
	n->namelen = len;
	memcpy(n->name, name, len);
	n->name[len] = '\0';
	n->upper_inode = u_inode;
	n->lower_inode = l_inode;
	n->next = osb->nodes;
	osb->nodes = n;
	return n;
}

static int ovl_lookup(struct inode *dir, const char *name, int len,
		      struct inode **result)
{
	struct super_block *sb;
	struct ovl_node *dir_node, *child_node;
	struct inode *u_inode = NULL, *l_inode = NULL;
	int wh = 0;

	*result = NULL;
	if (!dir)
		return -ENOENT;
	if (!S_ISDIR(dir->i_mode)) {
		iput(dir);
		return -ENOTDIR;
	}
	sb = dir->i_sb;
	dir_node = ovl_find_node(sb, dir->i_ino);
	if (!dir_node) {
		iput(dir);
		return -ENOENT;
	}

	if (len == 0 || (len == 1 && name[0] == '.')) {
		*result = dir;
		return 0;
	}
	if (len == 2 && name[0] == '.' && name[1] == '.') {
		unsigned long pino = dir_node->parent_ino;
		iput(dir);
		*result = iget(sb, pino);
		return *result ? 0 : -ENOENT;
	}
	if (len > OVL_NAME_LEN || ovl_is_whiteout_name(name, len)) {
		iput(dir);
		return -ENOENT;
	}

	ovl_refresh_node(sb, dir_node, dir);

	if (dir_node->upper_inode) {
		ovl_real_lookup(dir_node->upper_inode, name, len, &u_inode);
		wh = ovl_has_whiteout(dir_node->upper_inode, name, len);
	}
	if (!wh && dir_node->lower_inode) {
		ovl_real_lookup(dir_node->lower_inode, name, len, &l_inode);
	}

	if (!u_inode && !l_inode) {
		iput(dir);
		return -ENOENT;
	}

	child_node = ovl_get_or_create_child_node(sb, dir->i_ino, name, len,
						  u_inode, l_inode);
	iput(dir);
	if (!child_node)
		return -ENOMEM;

	*result = iget(sb, child_node->ino);
	if (!*result)
		return -EACCES;
	ovl_sync_inode_attrs(*result, child_node);
	return 0;
}

static int ovl_create(struct inode *dir, const char *name, int len, int mode,
		      struct inode **result)
{
	struct super_block *sb;
	struct ovl_node *dir_node, *child_node;
	struct inode *u_inode = NULL, *l_inode = NULL;
	int err;

	*result = NULL;
	if (!dir)
		return -ENOENT;
	sb = dir->i_sb;
	if (sb->s_flags & MS_RDONLY) {
		iput(dir);
		return -EROFS;
	}
	if (len <= 0 || len > OVL_NAME_LEN || ovl_is_whiteout_name(name, len)) {
		iput(dir);
		return -EINVAL;
	}

	dir_node = ovl_find_node(sb, dir->i_ino);
	if (!dir_node) {
		iput(dir);
		return -ENOENT;
	}
	err = ovl_ensure_upper_dir(sb, dir_node);
	if (err) {
		iput(dir);
		return err;
	}

	ovl_remove_whiteout(dir_node->upper_inode, name, len);
	err = ovl_real_create(dir_node->upper_inode, name, len, mode, &u_inode);
	if (err || !u_inode) {
		iput(dir);
		return err ? err : -EIO;
	}

	if (dir_node->lower_inode)
		ovl_real_lookup(dir_node->lower_inode, name, len, &l_inode);

	child_node = ovl_get_or_create_child_node(sb, dir->i_ino, name, len,
						  u_inode, l_inode);
	iput(dir);
	if (!child_node)
		return -ENOMEM;

	*result = iget(sb, child_node->ino);
	if (!*result)
		return -ENOSPC;
	ovl_sync_inode_attrs(*result, child_node);
	return 0;
}

static int ovl_unlink(struct inode *dir, const char *name, int len)
{
	struct super_block *sb;
	struct ovl_node *dir_node, *child_node;
	struct inode *u_inode = NULL, *l_inode = NULL;
	int err = 0;

	if (!dir)
		return -ENOENT;
	sb = dir->i_sb;
	if (sb->s_flags & MS_RDONLY) {
		iput(dir);
		return -EROFS;
	}
	dir_node = ovl_find_node(sb, dir->i_ino);
	if (!dir_node) {
		iput(dir);
		return -ENOENT;
	}

	if (dir_node->upper_inode)
		ovl_real_lookup(dir_node->upper_inode, name, len, &u_inode);
	if (dir_node->lower_inode)
		ovl_real_lookup(dir_node->lower_inode, name, len, &l_inode);

	if (!u_inode && !l_inode) {
		iput(dir);
		return -ENOENT;
	}

	if (u_inode) {
		iput(u_inode);
		err = ovl_real_unlink(dir_node->upper_inode, name, len);
		if (err) {
			if (l_inode)
				iput(l_inode);
			iput(dir);
			return err;
		}
	}

	/* If file exists in lowerdir, plant a .wh.<name> whiteout in upperdir */
	if (l_inode) {
		iput(l_inode);
		err = ovl_ensure_upper_dir(sb, dir_node);
		if (!err)
			err = ovl_create_whiteout(dir_node->upper_inode, name, len);
	}

	child_node = ovl_find_child_node(sb, dir->i_ino, name, len);
	if (child_node && child_node->upper_inode) {
		iput(child_node->upper_inode);
		child_node->upper_inode = NULL;
	}

	iput(dir);
	return err;
}

static int ovl_mkdir(struct inode *dir, const char *name, int len, int mode)
{
	struct super_block *sb;
	struct ovl_node *dir_node;
	int err;

	if (!dir)
		return -ENOENT;
	sb = dir->i_sb;
	if (sb->s_flags & MS_RDONLY) {
		iput(dir);
		return -EROFS;
	}
	dir_node = ovl_find_node(sb, dir->i_ino);
	if (!dir_node) {
		iput(dir);
		return -ENOENT;
	}
	err = ovl_ensure_upper_dir(sb, dir_node);
	if (err) {
		iput(dir);
		return err;
	}
	ovl_remove_whiteout(dir_node->upper_inode, name, len);
	err = ovl_real_mkdir(dir_node->upper_inode, name, len, mode);
	iput(dir);
	return err;
}

static int ovl_rmdir(struct inode *dir, const char *name, int len)
{
	struct super_block *sb;
	struct ovl_node *dir_node;
	struct inode *u_inode = NULL, *l_inode = NULL;
	int err = 0;

	if (!dir)
		return -ENOENT;
	sb = dir->i_sb;
	if (sb->s_flags & MS_RDONLY) {
		iput(dir);
		return -EROFS;
	}
	dir_node = ovl_find_node(sb, dir->i_ino);
	if (!dir_node) {
		iput(dir);
		return -ENOENT;
	}
	if (dir_node->upper_inode)
		ovl_real_lookup(dir_node->upper_inode, name, len, &u_inode);
	if (dir_node->lower_inode)
		ovl_real_lookup(dir_node->lower_inode, name, len, &l_inode);

	if (!u_inode && !l_inode) {
		iput(dir);
		return -ENOENT;
	}
	if (u_inode) {
		iput(u_inode);
		err = ovl_real_rmdir(dir_node->upper_inode, name, len);
		if (err) {
			if (l_inode)
				iput(l_inode);
			iput(dir);
			return err;
		}
	}
	if (l_inode) {
		iput(l_inode);
		err = ovl_ensure_upper_dir(sb, dir_node);
		if (!err)
			err = ovl_create_whiteout(dir_node->upper_inode, name, len);
	}
	iput(dir);
	return err;
}

static int ovl_symlink_readlink(struct inode *inode, char *buffer, int buflen)
{
	struct ovl_node *node;
	struct inode *real;

	if (!inode || !inode->i_sb)
		return -EINVAL;
	node = ovl_find_node(inode->i_sb, inode->i_ino);
	real = ovl_active_inode(node);
	if (!real || !real->i_op || !real->i_op->readlink) {
		iput(inode);
		return -EINVAL;
	}
	real->i_count++;
	iput(inode);
	return real->i_op->readlink(real, buffer, buflen);
}

static int ovl_symlink_follow_link(struct inode *dir, struct inode *inode,
				   int flag, int mode, struct inode **res_inode)
{
	struct ovl_node *node;
	struct inode *real;

	*res_inode = NULL;
	if (!dir || !inode) {
		if (dir)
			iput(dir);
		if (inode)
			iput(inode);
		return -ENOENT;
	}
	node = ovl_find_node(inode->i_sb, inode->i_ino);
	real = ovl_active_inode(node);
	if (!real || !real->i_op || !real->i_op->follow_link) {
		iput(dir);
		iput(inode);
		return -EINVAL;
	}
	real->i_count++;
	iput(inode);
	return real->i_op->follow_link(dir, real, flag, mode, res_inode);
}

static struct file_operations ovl_dir_operations = {
	NULL,			/* lseek */
	NULL,			/* read */
	NULL,			/* write */
	ovl_readdir,		/* readdir */
	NULL,			/* select */
	NULL,			/* ioctl */
	NULL,			/* mmap */
	NULL,			/* open */
	NULL,			/* release */
	file_fsync,		/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL			/* revalidate */
};

struct inode_operations ovl_dir_inode_operations = {
	&ovl_dir_operations,	/* default directory file-ops */
	ovl_create,		/* create */
	ovl_lookup,		/* lookup */
	NULL,			/* link */
	ovl_unlink,		/* unlink */
	NULL,			/* symlink */
	ovl_mkdir,		/* mkdir */
	ovl_rmdir,		/* rmdir */
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

struct inode_operations ovl_symlink_inode_operations = {
	NULL,			/* default file-ops */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	ovl_symlink_readlink,	/* readlink */
	ovl_symlink_follow_link,/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL			/* smap */
};

/*
 * Superblock operations
 */
static void ovl_read_inode(struct inode *inode)
{
	struct ovl_node *node;

	if (!inode || !inode->i_sb)
		return;
	node = ovl_find_node(inode->i_sb, inode->i_ino);
	if (!node)
		return;
	ovl_sync_inode_attrs(inode, node);
}

static void ovl_write_inode(struct inode *inode)
{
	if (inode)
		inode->i_dirt = 0;
}

static void ovl_put_inode(struct inode *inode)
{
	if (inode && inode->i_nlink == 0)
		inode->i_size = 0;
}

static int ovl_notify_change(struct inode *inode, struct iattr *attr)
{
	struct ovl_node *node;
	int err;

	if (!inode || !inode->i_sb)
		return -EINVAL;
	if (inode->i_sb->s_flags & MS_RDONLY)
		return -EROFS;
	err = ovl_copy_up(inode);
	if (err)
		return err;
	node = ovl_find_node(inode->i_sb, inode->i_ino);
	if (!node || !node->upper_inode)
		return -EROFS;
	inode_setattr(node->upper_inode, attr);
	ovl_sync_inode_attrs(inode, node);
	return 0;
}

static void ovl_put_super(struct super_block *sb)
{
	struct ovl_sb *osb = ovl_get_sb(sb);
	struct ovl_node *n, *next;

	if (!osb)
		return;
	for (n = osb->nodes; n; n = next) {
		next = n->next;
		if (n->upper_inode)
			iput(n->upper_inode);
		if (n->lower_inode)
			iput(n->lower_inode);
		kfree(n);
	}
	sb->u.generic_sbp = NULL;
	sb->s_dev = 0;
	kfree(osb);
}

static void ovl_statfs(struct super_block *sb, struct statfs *buf, int bufsiz)
{
	struct ovl_sb *osb = ovl_get_sb(sb);
	struct statfs tmp;

	memset(&tmp, 0, sizeof(tmp));
	tmp.f_type = OVERLAYFS_SUPER_MAGIC;
	tmp.f_bsize = PAGE_SIZE;
	tmp.f_namelen = OVL_NAME_LEN;

	if (osb && osb->upper_root && osb->upper_root->i_sb &&
	    osb->upper_root->i_sb->s_op &&
	    osb->upper_root->i_sb->s_op->statfs) {
		unsigned long old_fs = get_fs();
		set_fs(get_ds());
		osb->upper_root->i_sb->s_op->statfs(
			osb->upper_root->i_sb, &tmp, sizeof(tmp));
		set_fs(old_fs);
		tmp.f_type = OVERLAYFS_SUPER_MAGIC;
	}
	memcpy_tofs(buf, &tmp, bufsiz);
}

static struct super_operations ovl_sops = {
	ovl_read_inode,		/* read_inode */
	ovl_notify_change,	/* notify_change */
	ovl_write_inode,	/* write_inode */
	ovl_put_inode,		/* put_inode */
	ovl_put_super,		/* put_super */
	NULL,			/* write_super */
	ovl_statfs,		/* statfs */
	NULL			/* remount_fs */
};

static void ovl_parse_options(char *options, char *lowerdir, char *upperdir)
{
	char *p, *tok, *val;

	lowerdir[0] = '\0';
	upperdir[0] = '\0';
	if (!options)
		return;

	p = options;
	while (p && *p) {
		tok = p;
		p = strchr(tok, ',');
		if (p)
			*p++ = '\0';
		val = strchr(tok, '=');
		if (!val)
			continue;
		*val++ = '\0';
		if (strcmp(tok, "lowerdir") == 0) {
			strncpy(lowerdir, val, OVL_PATH_LEN - 1);
			lowerdir[OVL_PATH_LEN - 1] = '\0';
		} else if (strcmp(tok, "upperdir") == 0) {
			strncpy(upperdir, val, OVL_PATH_LEN - 1);
			upperdir[OVL_PATH_LEN - 1] = '\0';
		}
	}
}

struct super_block *overlay_read_super(struct super_block *sb, void *data,
				       int silent)
{
	struct ovl_sb *osb;
	struct ovl_node *root_node;
	struct inode *lower_root = NULL, *upper_root = NULL;
	char opt_buf[256];
	char lower_path[OVL_PATH_LEN], upper_path[OVL_PATH_LEN];
	unsigned long old_fs;
	int err;

	if (data) {
		strncpy(opt_buf, (char *)data, sizeof(opt_buf) - 1);
		opt_buf[sizeof(opt_buf) - 1] = '\0';
	} else {
		strcpy(opt_buf, "lowerdir=/bin,upperdir=/var/overlay/bin");
	}
	ovl_parse_options(opt_buf, lower_path, upper_path);
	if (!lower_path[0])
		strcpy(lower_path, "/bin");
	if (!upper_path[0])
		strcpy(upper_path, "/var/overlay/bin");

	old_fs = get_fs();
	set_fs(get_ds());
	err = namei(lower_path, &lower_root);
	if (err || !lower_root) {
		set_fs(old_fs);
		if (!silent)
			printk("overlayfs: failed to lookup lowerdir '%s' (err=%d)\n",
			       lower_path, err);
		return NULL;
	}
	err = namei(upper_path, &upper_root);
	set_fs(old_fs);
	if (err || !upper_root) {
		iput(lower_root);
		if (!silent)
			printk("overlayfs: failed to lookup upperdir '%s' (err=%d)\n",
			       upper_path, err);
		return NULL;
	}

	/*
	 * Also support the case where lowerdir and upperdir are both '/bin'
	 * and '/bin' is a mount point: use s_mounted as lower_root and
	 * s_covered (/bin on /dev/hda) as upper_root!
	 */
	if (lower_root == upper_root &&
	    lower_root->i_sb &&
	    lower_root == lower_root->i_sb->s_mounted &&
	    lower_root->i_sb->s_covered != NULL) {
		iput(upper_root);
		upper_root = lower_root->i_sb->s_covered;
		upper_root->i_count++;
	}

	if (!S_ISDIR(lower_root->i_mode) || !S_ISDIR(upper_root->i_mode)) {
		iput(lower_root);
		iput(upper_root);
		return NULL;
	}

	osb = (struct ovl_sb *)kmalloc(sizeof(struct ovl_sb), GFP_KERNEL);
	if (!osb) {
		iput(lower_root);
		iput(upper_root);
		return NULL;
	}
	memset(osb, 0, sizeof(*osb));
	osb->lower_root = lower_root;
	osb->upper_root = upper_root;
	strcpy(osb->lowerdir, lower_path);
	strcpy(osb->upperdir, upper_path);
	osb->next_ino = OVL_ROOT_INO + 1;

	root_node = (struct ovl_node *)kmalloc(sizeof(struct ovl_node), GFP_KERNEL);
	if (!root_node) {
		iput(lower_root);
		iput(upper_root);
		kfree(osb);
		return NULL;
	}
	memset(root_node, 0, sizeof(*root_node));
	root_node->ino = OVL_ROOT_INO;
	root_node->parent_ino = OVL_ROOT_INO;
	root_node->upper_inode = upper_root;
	root_node->lower_inode = lower_root;
	osb->nodes = root_node;

	sb->s_magic = OVERLAYFS_SUPER_MAGIC;
	sb->s_blocksize = PAGE_SIZE;
	sb->s_blocksize_bits = PAGE_SHIFT;
	sb->u.generic_sbp = osb;
	sb->s_op = &ovl_sops;

	sb->s_mounted = iget(sb, OVL_ROOT_INO);
	if (!sb->s_mounted) {
		ovl_put_super(sb);
		return NULL;
	}
	return sb;
}

static struct file_system_type overlay_fs_type = {
	overlay_read_super,
	"overlay",
	0,
	NULL
};

int ovl_format_mount_opts(struct super_block *sb, char *buf)
{
	struct ovl_sb *osb = ovl_get_sb(sb);

	if (!osb)
		return 0;
	return sprintf(buf, ",lowerdir=%s,upperdir=%s",
		       osb->lowerdir, osb->upperdir);
}

int init_overlay_fs(void)
{
	return register_filesystem(&overlay_fs_type);
}
