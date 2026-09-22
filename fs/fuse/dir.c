/*
 * linux/fs/fuse/dir.c
 *
 * Directory, namespace, symlink, and permission operations for the SIX
 * (Linux 2.0.11) FUSE driver.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <asm/segment.h>

#include "fuse_i.h"

/*
 * Helper to copy a (name, len) slice into a NUL-terminated kernel buffer.
 */
static char *fuse_dup_name(const char *name, int len)
{
	char *buf;

	if (len < 0 || len > 4096)
		return NULL;
	buf = (char *)kmalloc((unsigned int)len + 1, GFP_KERNEL);
	if (!buf)
		return NULL;
	memcpy(buf, name, (unsigned int)len);
	buf[len] = '\0';
	return buf;
}

/*
 * Helper to pack two NUL-terminated strings (s1 of len1, s2 of len2)
 * contiguously into a single kernel buffer, as required by FUSE_RENAME
 * and FUSE_SYMLINK.
 */
static char *fuse_dup_two_names(const char *s1, int len1,
				const char *s2, int len2,
				unsigned int *total_out)
{
	char *buf;
	unsigned int total = (unsigned int)len1 + 1 + (unsigned int)len2 + 1;

	buf = (char *)kmalloc(total, GFP_KERNEL);
	if (!buf)
		return NULL;
	memcpy(buf, s1, (unsigned int)len1);
	buf[len1] = '\0';
	memcpy(buf + len1 + 1, s2, (unsigned int)len2);
	buf[total - 1] = '\0';
	*total_out = total;
	return buf;
}

static int fuse_lookup(struct inode *dir, const char *name, int len,
		       struct inode **result)
{
	struct fuse_conn *fc;
	struct fuse_req req;
	struct fuse_entry_out outarg;
	char *kname;
	int err;

	*result = NULL;
	if (!dir)
		return -ENOENT;
	if (!S_ISDIR(dir->i_mode)) {
		iput(dir);
		return -ENOTDIR;
	}

	if (len == 0 || (len == 1 && name[0] == '.')) {
		dir->i_count++;
		*result = dir;
		iput(dir);
		return 0;
	}

	if (len == 2 && name[0] == '.' && name[1] == '.' &&
	    dir->i_ino == FUSE_ROOT_ID) {
		dir->i_count++;
		*result = dir;
		iput(dir);
		return 0;
	}

	fc = fuse_get_conn(dir->i_sb);
	if (!fc) {
		iput(dir);
		return -ENOTCONN;
	}

	kname = fuse_dup_name(name, len);
	if (!kname) {
		iput(dir);
		return -ENOMEM;
	}

	fuse_req_init(&req);
	memset(&outarg, 0, sizeof(outarg));

	req.in.opcode = FUSE_LOOKUP;
	req.in.nodeid = fuse_get_nodeid(dir);
	req.in_payload = kname;
	req.in_payload_len = (unsigned int)len + 1;
	req.out_buf = &outarg;
	req.out_buf_max = sizeof(outarg);

	err = fuse_request_send(fc, &req);
	kfree(kname);

	if (err) {
		iput(dir);
		return err;
	}
	if (req.out_buf_actual < FUSE_COMPAT_ENTRY_OUT_SIZE ||
	    outarg.nodeid == 0) {
		iput(dir);
		return -ENOENT;
	}

	*result = fuse_iget(dir->i_sb, &outarg);
	iput(dir);
	return *result ? 0 : -EACCES;
}

static int fuse_mknod(struct inode *dir, const char *name, int len,
		      int mode, int rdev)
{
	struct fuse_conn *fc = fuse_get_conn(dir->i_sb);
	struct fuse_req req;
	struct fuse_mknod_in *inarg;
	struct fuse_entry_out outarg;
	struct inode *inode;
	char *kname;
	int err;

	if (!fc) {
		iput(dir);
		return -ENOTCONN;
	}

	kname = fuse_dup_name(name, len);
	if (!kname) {
		iput(dir);
		return -ENOMEM;
	}

	fuse_req_init(&req);
	memset(&outarg, 0, sizeof(outarg));

	req.in.opcode = FUSE_MKNOD;
	req.in.nodeid = fuse_get_nodeid(dir);

	inarg = (struct fuse_mknod_in *)req.in_arg;
	inarg->mode = (fuse_u32)mode;
	inarg->rdev = (fuse_u32)rdev;
	inarg->umask = (fuse_u32)current->fs->umask;
	req.in_arg_len = (fc->minor < 12) ? FUSE_COMPAT_MKNOD_IN_SIZE
					  : sizeof(*inarg);
	req.in_payload = kname;
	req.in_payload_len = (unsigned int)len + 1;
	req.out_buf = &outarg;
	req.out_buf_max = sizeof(outarg);

	err = fuse_request_send(fc, &req);
	kfree(kname);

	if (!err && req.out_buf_actual >= FUSE_COMPAT_ENTRY_OUT_SIZE) {
		inode = fuse_iget(dir->i_sb, &outarg);
		if (inode)
			iput(inode);
	}

	iput(dir);
	return err;
}

/*
 * Create a regular file.
 *
 * We first try FUSE_CREATE (which atomically creates and opens the file,
 * returning both fuse_entry_out and fuse_open_out in one reply).  We stash
 * the returned file handle in inode->u.fuse_i so the immediately-following
 * do_open() -> fuse_open() adopts it without sending a redundant FUSE_OPEN.
 *
 * If the daemon returns -ENOSYS for FUSE_CREATE, we memoize fc->no_create
 * and fall back to FUSE_MKNOD + FUSE_LOOKUP (and do_open() will then issue
 * FUSE_OPEN normally).
 */
static int fuse_create(struct inode *dir, const char *name, int len,
		       int mode, struct inode **result)
{
	struct fuse_conn *fc = fuse_get_conn(dir->i_sb);
	struct fuse_req req;
	struct fuse_create_in *inarg;
	unsigned char outbuf[sizeof(struct fuse_entry_out) + sizeof(struct fuse_open_out)];
	struct fuse_entry_out *eout;
	struct fuse_open_out *oout;
	unsigned int entry_sz;
	char *kname;
	int err;

	*result = NULL;
	if (!fc) {
		iput(dir);
		return -ENOTCONN;
	}

	if (!(mode & S_IFMT))
		mode |= S_IFREG;

	if (!fc->no_create) {
		kname = fuse_dup_name(name, len);
		if (!kname) {
			iput(dir);
			return -ENOMEM;
		}

		fuse_req_init(&req);
		memset(outbuf, 0, sizeof(outbuf));

		req.in.opcode = FUSE_CREATE;
		req.in.nodeid = fuse_get_nodeid(dir);

		inarg = (struct fuse_create_in *)req.in_arg;
		inarg->flags = O_CREAT | O_RDWR;
		inarg->mode = (fuse_u32)mode;
		inarg->umask = (fuse_u32)current->fs->umask;
		req.in_arg_len = (fc->minor < 12) ? FUSE_COMPAT_CREATE_IN_SIZE
						  : sizeof(*inarg);
		req.in_payload = kname;
		req.in_payload_len = (unsigned int)len + 1;
		req.out_buf = outbuf;
		req.out_buf_max = sizeof(outbuf);

		err = fuse_request_send(fc, &req);
		kfree(kname);

		if (err == -ENOSYS) {
			fc->no_create = 1;
		} else if (err) {
			iput(dir);
			return err;
		} else {
			entry_sz = fuse_entry_out_size(fc);
			if (req.out_buf_actual < entry_sz + sizeof(struct fuse_open_out)) {
				iput(dir);
				return -EIO;
			}
			eout = (struct fuse_entry_out *)outbuf;
			oout = (struct fuse_open_out *)(outbuf + entry_sz);
			*result = fuse_iget(dir->i_sb, eout);
			if (*result) {
				(*result)->u.fuse_i.has_create_fh = 1;
				(*result)->u.fuse_i.create_fh = oout->fh;
				(*result)->u.fuse_i.create_open_flags = oout->open_flags;
			}
			iput(dir);
			return *result ? 0 : -EACCES;
		}
	}

	/* Fallback path: FUSE_MKNOD followed by FUSE_LOOKUP */
	dir->i_count++;
	err = fuse_mknod(dir, name, len, mode, 0);
	if (err) {
		iput(dir);
		return err;
	}
	return fuse_lookup(dir, name, len, result);
}

static int fuse_mkdir(struct inode *dir, const char *name, int len, int mode)
{
	struct fuse_conn *fc = fuse_get_conn(dir->i_sb);
	struct fuse_req req;
	struct fuse_mkdir_in *inarg;
	struct fuse_entry_out outarg;
	struct inode *inode;
	char *kname;
	int err;

	if (!fc) {
		iput(dir);
		return -ENOTCONN;
	}

	kname = fuse_dup_name(name, len);
	if (!kname) {
		iput(dir);
		return -ENOMEM;
	}

	fuse_req_init(&req);
	memset(&outarg, 0, sizeof(outarg));

	req.in.opcode = FUSE_MKDIR;
	req.in.nodeid = fuse_get_nodeid(dir);

	inarg = (struct fuse_mkdir_in *)req.in_arg;
	inarg->mode = (fuse_u32)mode;
	inarg->umask = (fuse_u32)current->fs->umask;
	req.in_arg_len = sizeof(*inarg);
	req.in_payload = kname;
	req.in_payload_len = (unsigned int)len + 1;
	req.out_buf = &outarg;
	req.out_buf_max = sizeof(outarg);

	err = fuse_request_send(fc, &req);
	kfree(kname);

	if (!err && req.out_buf_actual >= FUSE_COMPAT_ENTRY_OUT_SIZE) {
		inode = fuse_iget(dir->i_sb, &outarg);
		if (inode)
			iput(inode);
	}

	iput(dir);
	return err;
}

static int fuse_unlink(struct inode *dir, const char *name, int len)
{
	struct fuse_conn *fc = fuse_get_conn(dir->i_sb);
	struct fuse_req req;
	char *kname;
	int err;

	if (!fc) {
		iput(dir);
		return -ENOTCONN;
	}

	kname = fuse_dup_name(name, len);
	if (!kname) {
		iput(dir);
		return -ENOMEM;
	}

	fuse_req_init(&req);
	req.in.opcode = FUSE_UNLINK;
	req.in.nodeid = fuse_get_nodeid(dir);
	req.in_payload = kname;
	req.in_payload_len = (unsigned int)len + 1;

	err = fuse_request_send(fc, &req);
	kfree(kname);
	iput(dir);
	return err;
}

static int fuse_rmdir(struct inode *dir, const char *name, int len)
{
	struct fuse_conn *fc = fuse_get_conn(dir->i_sb);
	struct fuse_req req;
	char *kname;
	int err;

	if (!fc) {
		iput(dir);
		return -ENOTCONN;
	}

	kname = fuse_dup_name(name, len);
	if (!kname) {
		iput(dir);
		return -ENOMEM;
	}

	fuse_req_init(&req);
	req.in.opcode = FUSE_RMDIR;
	req.in.nodeid = fuse_get_nodeid(dir);
	req.in_payload = kname;
	req.in_payload_len = (unsigned int)len + 1;

	err = fuse_request_send(fc, &req);
	kfree(kname);
	iput(dir);
	return err;
}

static int fuse_symlink(struct inode *dir, const char *name, int len,
			const char *symname)
{
	struct fuse_conn *fc = fuse_get_conn(dir->i_sb);
	struct fuse_req req;
	struct fuse_entry_out outarg;
	struct inode *inode;
	char *payload;
	unsigned int payload_len;
	int err;

	if (!fc) {
		iput(dir);
		return -ENOTCONN;
	}

	payload = fuse_dup_two_names(name, len, symname, strlen(symname),
				     &payload_len);
	if (!payload) {
		iput(dir);
		return -ENOMEM;
	}

	fuse_req_init(&req);
	memset(&outarg, 0, sizeof(outarg));

	req.in.opcode = FUSE_SYMLINK;
	req.in.nodeid = fuse_get_nodeid(dir);
	req.in_payload = payload;
	req.in_payload_len = payload_len;
	req.out_buf = &outarg;
	req.out_buf_max = sizeof(outarg);

	err = fuse_request_send(fc, &req);
	kfree(payload);

	if (!err && req.out_buf_actual >= FUSE_COMPAT_ENTRY_OUT_SIZE) {
		inode = fuse_iget(dir->i_sb, &outarg);
		if (inode)
			iput(inode);
	}

	iput(dir);
	return err;
}

static int fuse_link(struct inode *oldinode, struct inode *dir,
		     const char *name, int len)
{
	struct fuse_conn *fc = fuse_get_conn(dir->i_sb);
	struct fuse_req req;
	struct fuse_link_in *inarg;
	struct fuse_entry_out outarg;
	struct inode *inode;
	char *kname;
	int err;

	if (!fc) {
		iput(oldinode);
		iput(dir);
		return -ENOTCONN;
	}

	kname = fuse_dup_name(name, len);
	if (!kname) {
		iput(oldinode);
		iput(dir);
		return -ENOMEM;
	}

	fuse_req_init(&req);
	memset(&outarg, 0, sizeof(outarg));

	req.in.opcode = FUSE_LINK;
	req.in.nodeid = fuse_get_nodeid(dir);

	inarg = (struct fuse_link_in *)req.in_arg;
	inarg->oldnodeid = fuse_get_nodeid(oldinode);
	req.in_arg_len = sizeof(*inarg);
	req.in_payload = kname;
	req.in_payload_len = (unsigned int)len + 1;
	req.out_buf = &outarg;
	req.out_buf_max = sizeof(outarg);

	err = fuse_request_send(fc, &req);
	kfree(kname);

	if (!err && req.out_buf_actual >= FUSE_COMPAT_ENTRY_OUT_SIZE) {
		inode = fuse_iget(dir->i_sb, &outarg);
		if (inode)
			iput(inode);
	}

	iput(oldinode);
	iput(dir);
	return err;
}

static int fuse_rename(struct inode *old_dir, const char *old_name, int old_len,
		       struct inode *new_dir, const char *new_name, int new_len,
		       int must_be_dir)
{
	struct fuse_conn *fc = fuse_get_conn(old_dir->i_sb);
	struct fuse_req req;
	struct fuse_rename_in *inarg;
	char *payload;
	unsigned int payload_len;
	int err;

	if (!fc) {
		iput(old_dir);
		iput(new_dir);
		return -ENOTCONN;
	}

	payload = fuse_dup_two_names(old_name, old_len, new_name, new_len,
				     &payload_len);
	if (!payload) {
		iput(old_dir);
		iput(new_dir);
		return -ENOMEM;
	}

	fuse_req_init(&req);
	req.in.opcode = FUSE_RENAME;
	req.in.nodeid = fuse_get_nodeid(old_dir);

	inarg = (struct fuse_rename_in *)req.in_arg;
	inarg->newdir = fuse_get_nodeid(new_dir);
	req.in_arg_len = sizeof(*inarg);
	req.in_payload = payload;
	req.in_payload_len = payload_len;

	err = fuse_request_send(fc, &req);
	kfree(payload);
	iput(old_dir);
	iput(new_dir);
	return err;
}

static int fuse_readlink_common(struct inode *inode, char *kbuf, int maxlen)
{
	struct fuse_conn *fc = fuse_get_conn(inode->i_sb);
	struct fuse_req req;
	int err;

	if (!fc)
		return -ENOTCONN;

	fuse_req_init(&req);
	req.in.opcode = FUSE_READLINK;
	req.in.nodeid = fuse_get_nodeid(inode);
	req.out_buf = kbuf;
	req.out_buf_max = (unsigned int)(maxlen - 1);

	err = fuse_request_send(fc, &req);
	if (err)
		return err;

	kbuf[req.out_buf_actual] = '\0';
	return (int)req.out_buf_actual;
}

static int fuse_readlink(struct inode *inode, char *buffer, int buflen)
{
	char *kbuf;
	int len;

	if (!S_ISLNK(inode->i_mode)) {
		iput(inode);
		return -EINVAL;
	}
	if (buflen > 4095)
		buflen = 4095;

	kbuf = (char *)kmalloc((unsigned int)buflen + 1, GFP_KERNEL);
	if (!kbuf) {
		iput(inode);
		return -ENOMEM;
	}

	len = fuse_readlink_common(inode, kbuf, buflen + 1);
	iput(inode);
	if (len >= 0)
		memcpy_tofs(buffer, kbuf, (unsigned int)len);
	kfree(kbuf);
	return len;
}

static int fuse_follow_link(struct inode *dir, struct inode *inode,
			    int flag, int mode, struct inode **res_inode)
{
	char *kbuf;
	int err;

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
		iput(inode);
		iput(dir);
		return -ELOOP;
	}

	kbuf = (char *)kmalloc(4096, GFP_KERNEL);
	if (!kbuf) {
		iput(inode);
		iput(dir);
		return -ENOMEM;
	}

	err = fuse_readlink_common(inode, kbuf, 4096);
	iput(inode);
	if (err < 0) {
		kfree(kbuf);
		iput(dir);
		return err;
	}

	current->link_count++;
	err = open_namei(kbuf, flag, mode, res_inode, dir);
	current->link_count--;
	kfree(kbuf);
	return err;
}

static int fuse_standard_permission(struct inode *inode, int mask)
{
	int mode = inode->i_mode;

	if ((mask & MAY_WRITE) && IS_RDONLY(inode))
		return -EROFS;
	if (current->fsuid == inode->i_uid)
		mode >>= 6;
	else if (in_group_p(inode->i_gid))
		mode >>= 3;
	if (((mode & mask & 0007) == mask) || fsuser())
		return 0;
	return -EACCES;
}

int fuse_permission(struct inode *inode, int mask)
{
	struct fuse_conn *fc = fuse_get_conn(inode->i_sb);
	struct fuse_req req;
	struct fuse_access_in *inarg;
	int err;

	if (!fc)
		return -ENOTCONN;

	if (fc->default_permissions || fc->no_access)
		return fuse_standard_permission(inode, mask);

	fuse_req_init(&req);
	req.in.opcode = FUSE_ACCESS;
	req.in.nodeid = fuse_get_nodeid(inode);

	inarg = (struct fuse_access_in *)req.in_arg;
	inarg->mask = (fuse_u32)mask;
	req.in_arg_len = sizeof(*inarg);

	err = fuse_request_send(fc, &req);
	if (err == -ENOSYS) {
		fc->no_access = 1;
		return fuse_standard_permission(inode, mask);
	}
	return err;
}

static int fuse_dir_open(struct inode *inode, struct file *file)
{
	struct fuse_conn *fc = fuse_get_conn(inode->i_sb);
	struct fuse_file *ff;
	struct fuse_req req;
	struct fuse_open_in *inarg;
	struct fuse_open_out outarg;
	int err = 0;

	if (!fc)
		return -ENOTCONN;

	ff = (struct fuse_file *)kmalloc(sizeof(*ff), GFP_KERNEL);
	if (!ff)
		return -ENOMEM;
	memset(ff, 0, sizeof(*ff));
	ff->is_dir = 1;

	if (!fc->no_opendir) {
		fuse_req_init(&req);
		memset(&outarg, 0, sizeof(outarg));

		req.in.opcode = FUSE_OPENDIR;
		req.in.nodeid = fuse_get_nodeid(inode);

		inarg = (struct fuse_open_in *)req.in_arg;
		inarg->flags = file->f_flags;
		req.in_arg_len = sizeof(*inarg);
		req.out_buf = &outarg;
		req.out_buf_max = sizeof(outarg);

		err = fuse_request_send(fc, &req);
		if (err == -ENOSYS) {
			fc->no_opendir = 1;
			err = 0;
		} else if (!err && req.out_buf_actual >= sizeof(outarg)) {
			ff->fh = outarg.fh;
			ff->open_flags = outarg.open_flags;
		}
	}

	if (err) {
		kfree(ff);
		return err;
	}

	file->private_data = ff;
	return 0;
}

static void fuse_dir_release(struct inode *inode, struct file *file)
{
	struct fuse_conn *fc = fuse_get_conn(inode->i_sb);
	struct fuse_file *ff = (struct fuse_file *)file->private_data;
	struct fuse_req req;
	struct fuse_release_in *inarg;

	if (!ff)
		return;

	if (fc && fc->connected && !fc->no_releasedir) {
		fuse_req_init(&req);
		req.in.opcode = FUSE_RELEASEDIR;
		req.in.nodeid = fuse_get_nodeid(inode);

		inarg = (struct fuse_release_in *)req.in_arg;
		inarg->fh = ff->fh;
		inarg->flags = file->f_flags;
		req.in_arg_len = sizeof(*inarg);

		if (fuse_request_send(fc, &req) == -ENOSYS)
			fc->no_releasedir = 1;
	}

	kfree(ff);
	file->private_data = NULL;
}

static int fuse_dir_fsync(struct inode *inode, struct file *file)
{
	struct fuse_conn *fc = fuse_get_conn(inode->i_sb);
	struct fuse_file *ff = (struct fuse_file *)file->private_data;
	struct fuse_req req;
	struct fuse_fsync_in *inarg;
	int err;

	if (!fc)
		return -ENOTCONN;
	if (fc->no_fsyncdir)
		return 0;

	fuse_req_init(&req);
	req.in.opcode = FUSE_FSYNCDIR;
	req.in.nodeid = fuse_get_nodeid(inode);

	inarg = (struct fuse_fsync_in *)req.in_arg;
	inarg->fh = ff ? ff->fh : 0;
	inarg->fsync_flags = 0;
	req.in_arg_len = sizeof(*inarg);

	err = fuse_request_send(fc, &req);
	if (err == -ENOSYS) {
		fc->no_fsyncdir = 1;
		err = 0;
	}
	return err;
}

static int fuse_readdir(struct inode *inode, struct file *file,
			void *dirent, filldir_t filldir)
{
	struct fuse_conn *fc;
	struct fuse_file *ff;
	struct fuse_req req;
	struct fuse_read_in *inarg;
	char *kbuf;
	unsigned int pos, actual;
	int err;

	if (!inode || !S_ISDIR(inode->i_mode))
		return -ENOTDIR;

	fc = fuse_get_conn(inode->i_sb);
	if (!fc)
		return -ENOTCONN;

	ff = (struct fuse_file *)file->private_data;
	kbuf = (char *)kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	fuse_req_init(&req);
	req.in.opcode = FUSE_READDIR;
	req.in.nodeid = fuse_get_nodeid(inode);

	inarg = (struct fuse_read_in *)req.in_arg;
	inarg->fh = ff ? ff->fh : 0;
	inarg->offset = (fuse_u64)file->f_pos;
	inarg->size = PAGE_SIZE;
	inarg->flags = file->f_flags;
	req.in_arg_len = (fc->minor < 9) ? FUSE_COMPAT_READ_IN_SIZE
					 : sizeof(*inarg);
	req.out_buf = kbuf;
	req.out_buf_max = PAGE_SIZE;

	err = fuse_request_send(fc, &req);
	if (err) {
		kfree(kbuf);
		return err;
	}

	actual = req.out_buf_actual;
	pos = 0;
	while (pos + FUSE_NAME_OFFSET <= actual) {
		struct fuse_dirent *fde = (struct fuse_dirent *)(kbuf + pos);
		unsigned int entsize = FUSE_DIRENT_SIZE(fde);
		ino_t ino;

		if (!fde->namelen || pos + entsize > actual)
			break;

		ino = (ino_t)fde->ino;
		if (!ino)
			ino = 1;

		if (filldir(dirent, fde->name, (int)fde->namelen,
			    file->f_pos, ino) < 0)
			break;

		file->f_pos = (loff_t)fde->off;
		pos += entsize;
	}

	kfree(kbuf);
	return 0;
}

struct file_operations fuse_dir_operations = {
	NULL,			/* lseek */
	NULL,			/* read */
	NULL,			/* write */
	fuse_readdir,		/* readdir */
	NULL,			/* select */
	NULL,			/* ioctl */
	NULL,			/* mmap */
	fuse_dir_open,		/* open */
	fuse_dir_release,	/* release */
	fuse_dir_fsync,		/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL			/* revalidate */
};

struct inode_operations fuse_dir_inode_operations = {
	&fuse_dir_operations,	/* default_file_ops */
	fuse_create,		/* create */
	fuse_lookup,		/* lookup */
	fuse_link,		/* link */
	fuse_unlink,		/* unlink */
	fuse_symlink,		/* symlink */
	fuse_mkdir,		/* mkdir */
	fuse_rmdir,		/* rmdir */
	fuse_mknod,		/* mknod */
	fuse_rename,		/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	fuse_permission,	/* permission */
	NULL			/* smap */
};

struct inode_operations fuse_symlink_inode_operations = {
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
	fuse_readlink,		/* readlink */
	fuse_follow_link,	/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL			/* smap */
};
