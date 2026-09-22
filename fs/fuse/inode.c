/*
 * linux/fs/fuse/inode.c
 *
 * Superblock, mount option parsing, inode lifecycle, GETATTR/SETATTR/STATFS,
 * and filesystem registration for SIX (Linux 2.0.11).
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/fs.h>
#include <linux/locks.h>
#include <linux/major.h>
#include <asm/segment.h>

#include "fuse_i.h"

static unsigned long fuse_ino_from_nodeid(fuse_u64 nodeid)
{
	unsigned long ino;

	if (nodeid == FUSE_ROOT_ID)
		return (unsigned long)FUSE_ROOT_ID;

	ino = (unsigned long)nodeid ^ (unsigned long)(nodeid >> 32);
	if (ino == 0 || ino == FUSE_ROOT_ID)
		ino = 2;
	return ino;
}

static unsigned char fuse_blocksize_bits(unsigned long size)
{
	unsigned char bits = 9;

	while ((1UL << bits) < size && bits < 16)
		bits++;
	return bits;
}

void fuse_change_attributes(struct inode *inode, struct fuse_attr *attr,
			    fuse_u64 attr_valid)
{
	struct fuse_conn *fc = fuse_get_conn(inode->i_sb);

	inode->i_mode = attr->mode;
	inode->i_nlink = attr->nlink ? attr->nlink : 1;
	inode->i_uid = attr->uid;
	inode->i_gid = attr->gid;
	inode->i_rdev = attr->rdev;
	inode->i_size = (off_t)attr->size;
	inode->i_atime = (time_t)attr->atime;
	inode->i_mtime = (time_t)attr->mtime;
	inode->i_ctime = (time_t)attr->ctime;
	inode->i_blocks = (unsigned long)attr->blocks;

	if (fc && fc->minor >= 9 && attr->blksize)
		inode->i_blksize = attr->blksize;
	else if (inode->i_sb)
		inode->i_blksize = inode->i_sb->s_blocksize;
	else
		inode->i_blksize = FUSE_DEFAULT_BLKSIZE;

	if (attr_valid > 3600)
		attr_valid = 3600;
	inode->u.fuse_i.attr_valid = jiffies + (unsigned long)attr_valid * HZ;

	if (S_ISREG(inode->i_mode))
		inode->i_op = &fuse_file_inode_operations;
	else if (S_ISDIR(inode->i_mode))
		inode->i_op = &fuse_dir_inode_operations;
	else if (S_ISLNK(inode->i_mode))
		inode->i_op = &fuse_symlink_inode_operations;
	else if (S_ISCHR(inode->i_mode))
		inode->i_op = &chrdev_inode_operations;
	else if (S_ISBLK(inode->i_mode))
		inode->i_op = &blkdev_inode_operations;
	else if (S_ISFIFO(inode->i_mode))
		init_fifo(inode);
}

static void fuse_read_inode(struct inode *inode)
{
	struct fuse_conn *fc = fuse_get_conn(inode->i_sb);

	memset(&inode->u.fuse_i, 0, sizeof(inode->u.fuse_i));
	inode->u.fuse_i.nodeid = (fuse_u64)inode->i_ino;

	if (inode->i_ino == FUSE_ROOT_ID && fc) {
		inode->i_mode = fc->rootmode;
		inode->i_nlink = 2;
		inode->i_uid = fc->user_id;
		inode->i_gid = fc->group_id;
		inode->i_size = FUSE_DEFAULT_BLKSIZE;
		inode->i_blksize = fc->blksize;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		inode->i_op = &fuse_dir_inode_operations;
		inode->u.fuse_i.nlookup = 1;
		inode->u.fuse_i.attr_valid = 0;
	}
}

struct inode *fuse_iget(struct super_block *sb, struct fuse_entry_out *entry)
{
	struct inode *inode;
	unsigned long ino;

	if (!entry || entry->nodeid == 0)
		return NULL;

	ino = fuse_ino_from_nodeid(entry->nodeid);
	inode = iget(sb, ino);
	if (!inode)
		return NULL;

	inode->u.fuse_i.nodeid = entry->nodeid;
	inode->u.fuse_i.generation = entry->generation;
	inode->u.fuse_i.nlookup++;
	fuse_change_attributes(inode, &entry->attr, entry->attr_valid);
	return inode;
}

int fuse_do_getattr(struct inode *inode)
{
	struct fuse_conn *fc = fuse_get_conn(inode->i_sb);
	struct fuse_req req;
	struct fuse_attr_out outarg;
	int err;

	if (!fc)
		return -ENOTCONN;

	fuse_req_init(&req);
	memset(&outarg, 0, sizeof(outarg));

	req.in.opcode = FUSE_GETATTR;
	req.in.nodeid = fuse_get_nodeid(inode);
	if (fc->minor >= 9)
		req.in_arg_len = sizeof(struct fuse_getattr_in);

	req.out_buf = &outarg;
	req.out_buf_max = sizeof(outarg);

	err = fuse_request_send(fc, &req);
	if (err)
		return err;
	if (req.out_buf_actual < FUSE_COMPAT_ATTR_OUT_SIZE)
		return -EIO;

	fuse_change_attributes(inode, &outarg.attr, outarg.attr_valid);
	return 0;
}

int fuse_revalidate_stat(struct inode *inode)
{
	if (!inode || !inode->i_sb || inode->i_sb->s_magic != FUSE_SUPER_MAGIC)
		return 0;
	if (inode->u.fuse_i.attr_valid && jiffies < inode->u.fuse_i.attr_valid)
		return 0;
	return fuse_do_getattr(inode);
}

static int fuse_notify_change(struct inode *inode, struct iattr *attr)
{
	struct fuse_conn *fc = fuse_get_conn(inode->i_sb);
	struct fuse_req req;
	struct fuse_setattr_in *inarg;
	struct fuse_attr_out outarg;
	int err;

	if (!fc)
		return -ENOTCONN;

	err = inode_change_ok(inode, attr);
	if (err)
		return err;

	fuse_req_init(&req);
	memset(&outarg, 0, sizeof(outarg));

	req.in.opcode = FUSE_SETATTR;
	req.in.nodeid = fuse_get_nodeid(inode);

	inarg = (struct fuse_setattr_in *)req.in_arg;
	if (attr->ia_valid & ATTR_MODE) {
		inarg->valid |= FATTR_MODE;
		inarg->mode = attr->ia_mode;
	}
	if (attr->ia_valid & ATTR_UID) {
		inarg->valid |= FATTR_UID;
		inarg->uid = attr->ia_uid;
	}
	if (attr->ia_valid & ATTR_GID) {
		inarg->valid |= FATTR_GID;
		inarg->gid = attr->ia_gid;
	}
	if (attr->ia_valid & ATTR_SIZE) {
		inarg->valid |= FATTR_SIZE;
		inarg->size = (fuse_u64)attr->ia_size;
	}
	if (attr->ia_valid & ATTR_ATIME) {
		inarg->valid |= FATTR_ATIME;
		inarg->atime = (fuse_u64)attr->ia_atime;
		if (!(attr->ia_valid & ATTR_ATIME_SET))
			inarg->valid |= FATTR_ATIME_NOW;
	}
	if (attr->ia_valid & ATTR_MTIME) {
		inarg->valid |= FATTR_MTIME;
		inarg->mtime = (fuse_u64)attr->ia_mtime;
		if (!(attr->ia_valid & ATTR_MTIME_SET))
			inarg->valid |= FATTR_MTIME_NOW;
	}
	req.in_arg_len = sizeof(*inarg);
	req.out_buf = &outarg;
	req.out_buf_max = sizeof(outarg);

	err = fuse_request_send(fc, &req);
	if (err)
		return err;
	if (req.out_buf_actual < FUSE_COMPAT_ATTR_OUT_SIZE)
		return -EIO;

	fuse_change_attributes(inode, &outarg.attr, outarg.attr_valid);
	return 0;
}

static void fuse_put_inode(struct inode *inode)
{
	struct fuse_conn *fc = fuse_get_conn(inode->i_sb);

	if (inode->i_count != 1 || inode->i_ino == FUSE_ROOT_ID)
		return;

	if (fc && inode->u.fuse_i.nlookup > 0) {
		fuse_queue_forget(fc, fuse_get_nodeid(inode),
				  inode->u.fuse_i.nlookup);
		inode->u.fuse_i.nlookup = 0;
	}

	inode->i_nlink = 0;
	clear_inode(inode);
}

static void fuse_put_super(struct super_block *sb)
{
	struct fuse_conn *fc = fuse_get_conn(sb);

	if (fc) {
		if (fc->connected && fc->initialized && !fc->no_destroy) {
			struct fuse_req req;

			fuse_req_init(&req);
			req.in.opcode = FUSE_DESTROY;
			req.in.nodeid = 0;
			fuse_request_send(fc, &req);
		}
		fuse_conn_abort(fc);
		fc->mounted = 0;
		fuse_conn_put(fc);
		sb->u.fuse_sb.fc = NULL;
	}
	sb->s_dev = 0;
}

static void fuse_statfs(struct super_block *sb, struct statfs *buf, int bufsiz)
{
	struct fuse_conn *fc = fuse_get_conn(sb);
	struct statfs tmp;
	struct fuse_req req;
	struct fuse_statfs_out outarg;
	int err = -ENOSYS;

	memset(&tmp, 0, sizeof(tmp));
	tmp.f_type = FUSE_SUPER_MAGIC;
	tmp.f_bsize = sb->s_blocksize;
	tmp.f_namelen = 255;

	if (fc && !fc->no_statfs) {
		fuse_req_init(&req);
		memset(&outarg, 0, sizeof(outarg));
		req.in.opcode = FUSE_STATFS;
		req.in.nodeid = FUSE_ROOT_ID;
		req.out_buf = &outarg;
		req.out_buf_max = sizeof(outarg);

		err = fuse_request_send(fc, &req);
		if (err == -ENOSYS)
			fc->no_statfs = 1;
	}

	if (err == 0 && req.out_buf_actual >= sizeof(struct fuse_kstatfs)) {
		tmp.f_bsize = outarg.st.bsize ? (long)outarg.st.bsize : (long)sb->s_blocksize;
		tmp.f_blocks = (long)outarg.st.blocks;
		tmp.f_bfree = (long)outarg.st.bfree;
		tmp.f_bavail = (long)outarg.st.bavail;
		tmp.f_files = (long)outarg.st.files;
		tmp.f_ffree = (long)outarg.st.ffree;
		tmp.f_namelen = outarg.st.namelen ? (long)outarg.st.namelen : 255;
	}

	memcpy_tofs(buf, &tmp, bufsiz);
}

static struct super_operations fuse_sops = {
	fuse_read_inode,	/* read_inode */
	fuse_notify_change,	/* notify_change */
	NULL,			/* write_inode */
	fuse_put_inode,		/* put_inode */
	fuse_put_super,		/* put_super */
	NULL,			/* write_super */
	fuse_statfs,		/* statfs */
	NULL			/* remount_fs */
};

static unsigned long parse_ulong(const char *s, int base)
{
	unsigned long val = 0;

	while (*s) {
		int d;
		if (*s >= '0' && *s <= '9')
			d = *s - '0';
		else if (base == 16 && *s >= 'a' && *s <= 'f')
			d = *s - 'a' + 10;
		else if (base == 16 && *s >= 'A' && *s <= 'F')
			d = *s - 'A' + 10;
		else
			break;
		if (d >= base)
			break;
		val = val * (unsigned long)base + (unsigned long)d;
		s++;
	}
	return val;
}

static int fuse_parse_options(char *options, int *fd_out, umode_t *rootmode,
			      uid_t *uid, gid_t *gid, unsigned int *blksize,
			      unsigned int *max_read, int *def_perms,
			      int *allow_other)
{
	char *p = options;

	*fd_out = -1;
	*rootmode = S_IFDIR | 0755;
	*uid = current->fsuid;
	*gid = current->fsgid;
	*blksize = FUSE_DEFAULT_BLKSIZE;
	*max_read = FUSE_DEFAULT_MAX_READ;
	*def_perms = 0;
	*allow_other = 0;

	if (!p)
		return -EINVAL;

	while (*p) {
		char *next = strchr(p, ',');
		if (next)
			*next = '\0';

		if (!strncmp(p, "fd=", 3))
			*fd_out = (int)parse_ulong(p + 3, 10);
		else if (!strncmp(p, "rootmode=", 9))
			*rootmode = (umode_t)parse_ulong(p + 9, 8);
		else if (!strncmp(p, "user_id=", 8))
			*uid = (uid_t)parse_ulong(p + 8, 10);
		else if (!strncmp(p, "group_id=", 9))
			*gid = (gid_t)parse_ulong(p + 9, 10);
		else if (!strncmp(p, "blksize=", 8))
			*blksize = (unsigned int)parse_ulong(p + 8, 10);
		else if (!strncmp(p, "max_read=", 9))
			*max_read = (unsigned int)parse_ulong(p + 9, 10);
		else if (!strcmp(p, "default_permissions"))
			*def_perms = 1;
		else if (!strcmp(p, "allow_other") || !strcmp(p, "allow_root"))
			*allow_other = 1;

		if (next) {
			*next = ',';
			p = next + 1;
		} else {
			break;
		}
	}

	if (!(*rootmode & S_IFMT))
		*rootmode |= S_IFDIR;

	return (*fd_out >= 0) ? 0 : -EINVAL;
}

static struct super_block *fuse_read_super(struct super_block *sb,
					   void *data, int silent)
{
	struct fuse_conn *fc;
	struct inode *root_inode;
	int fd = -1, def_perms = 0, allow_other = 0;
	umode_t rootmode;
	uid_t uid;
	gid_t gid;
	unsigned int blksize, max_read;

	if (!data)
		return NULL;

	if (fuse_parse_options((char *)data, &fd, &rootmode, &uid, &gid,
			       &blksize, &max_read, &def_perms, &allow_other) != 0) {
		if (!silent)
			printk("FUSE: missing or invalid fd= mount option\n");
		return NULL;
	}

	fc = fuse_conn_from_fd((unsigned int)fd);
	if (!fc || !fc->connected || fc->mounted) {
		if (!silent)
			printk("FUSE: fd %d is not an unmounted /dev/fuse handle\n", fd);
		return NULL;
	}

	if (blksize < 512)
		blksize = FUSE_DEFAULT_BLKSIZE;

	fc->mounted = 1;
	fc->rootmode = rootmode;
	fc->user_id = uid;
	fc->group_id = gid;
	fc->blksize = blksize;
	fc->max_read = max_read;
	fc->default_permissions = def_perms;
	fc->allow_other = allow_other;
	fuse_conn_get(fc);

	lock_super(sb);
	sb->s_magic = FUSE_SUPER_MAGIC;
	sb->s_blocksize = blksize;
	sb->s_blocksize_bits = fuse_blocksize_bits(blksize);
	sb->s_op = &fuse_sops;
	sb->u.fuse_sb.fc = fc;
	unlock_super(sb);

	fuse_send_init(fc);

	root_inode = iget(sb, FUSE_ROOT_ID);
	if (!root_inode) {
		fc->mounted = 0;
		sb->u.fuse_sb.fc = NULL;
		fuse_conn_put(fc);
		return NULL;
	}

	sb->s_mounted = root_inode;
	return sb;
}

static struct file_system_type fuse_fs_type = {
	fuse_read_super, "fuse", 0, NULL
};

/*
 * Block-backed FUSE ("fuseblk", requires_dev = 1) used by filesystems such
 * as ntfs-3g when mounting a block device (/dev/hdb) via FUSE.
 */
static struct file_system_type fuseblk_fs_type = {
	fuse_read_super, "fuseblk", 1, NULL
};

int init_fuse_fs(void)
{
	if (register_chrdev(MISC_MAJOR, "fuse", &fuse_dev_fops))
		printk("FUSE: unable to register /dev/fuse on major %d\n",
		       MISC_MAJOR);
	register_filesystem(&fuse_fs_type);
	register_filesystem(&fuseblk_fs_type);
	return 0;
}
