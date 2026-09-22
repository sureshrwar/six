/*
 * linux/fs/fuse/file.c
 *
 * Regular file operations (open, release, flush, read, write, fsync, lseek,
 * readpage, bmap) for the SIX (Linux 2.0.11) FUSE driver.
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
#include <asm/segment.h>

#include "fuse_i.h"

#define FUSE_BOUNCE_SIZE	16384

static int fuse_file_open(struct inode *inode, struct file *file)
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

	/*
	 * If fuse_create() just issued FUSE_CREATE for this inode, adopt
	 * the file handle it returned instead of sending a second FUSE_OPEN.
	 */
	if (inode->u.fuse_i.has_create_fh) {
		ff->fh = inode->u.fuse_i.create_fh;
		ff->open_flags = inode->u.fuse_i.create_open_flags;
		inode->u.fuse_i.has_create_fh = 0;
		file->private_data = ff;
		return 0;
	}

	if (!fc->no_open) {
		fuse_req_init(&req);
		memset(&outarg, 0, sizeof(outarg));

		req.in.opcode = FUSE_OPEN;
		req.in.nodeid = fuse_get_nodeid(inode);

		inarg = (struct fuse_open_in *)req.in_arg;
		inarg->flags = file->f_flags & ~(O_CREAT | O_EXCL | O_NOCTTY);
		req.in_arg_len = sizeof(*inarg);
		req.out_buf = &outarg;
		req.out_buf_max = sizeof(outarg);

		err = fuse_request_send(fc, &req);
		if (err == -ENOSYS) {
			fc->no_open = 1;
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

static void fuse_file_release(struct inode *inode, struct file *file)
{
	struct fuse_conn *fc = fuse_get_conn(inode->i_sb);
	struct fuse_file *ff = (struct fuse_file *)file->private_data;
	struct fuse_req req;

	if (!ff)
		return;

	if (fc && fc->connected) {
		if (!fc->no_flush) {
			struct fuse_flush_in *fin;

			fuse_req_init(&req);
			req.in.opcode = FUSE_FLUSH;
			req.in.nodeid = fuse_get_nodeid(inode);

			fin = (struct fuse_flush_in *)req.in_arg;
			fin->fh = ff->fh;
			req.in_arg_len = sizeof(*fin);

			if (fuse_request_send(fc, &req) == -ENOSYS)
				fc->no_flush = 1;
		}

		if (!fc->no_open) {
			struct fuse_release_in *rin;

			fuse_req_init(&req);
			req.in.opcode = FUSE_RELEASE;
			req.in.nodeid = fuse_get_nodeid(inode);

			rin = (struct fuse_release_in *)req.in_arg;
			rin->fh = ff->fh;
			rin->flags = file->f_flags;
			req.in_arg_len = sizeof(*rin);

			fuse_request_send(fc, &req);
		}
	}

	kfree(ff);
	file->private_data = NULL;
}

static int fuse_file_read(struct inode *inode, struct file *file,
			  char *buf, int count)
{
	struct fuse_conn *fc;
	struct fuse_file *ff;
	char *kbuf;
	unsigned int max_chunk;
	int total_read = 0, err = 0;

	if (!inode)
		return -EINVAL;
	if (count <= 0)
		return 0;

	err = verify_area(VERIFY_WRITE, buf, count);
	if (err)
		return err;

	fc = fuse_get_conn(inode->i_sb);
	if (!fc)
		return -ENOTCONN;

	ff = (struct fuse_file *)file->private_data;
	max_chunk = FUSE_BOUNCE_SIZE;
	if (fc->max_read && fc->max_read < max_chunk)
		max_chunk = fc->max_read;

	kbuf = (char *)kmalloc(max_chunk, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	while (count > 0) {
		struct fuse_req req;
		struct fuse_read_in *inarg;
		unsigned int chunk = (unsigned int)count;

		if (chunk > max_chunk)
			chunk = max_chunk;

		fuse_req_init(&req);
		req.in.opcode = FUSE_READ;
		req.in.nodeid = fuse_get_nodeid(inode);

		inarg = (struct fuse_read_in *)req.in_arg;
		inarg->fh = ff ? ff->fh : 0;
		inarg->offset = (fuse_u64)file->f_pos;
		inarg->size = chunk;
		inarg->flags = file->f_flags;
		req.in_arg_len = (fc->minor < 9) ? FUSE_COMPAT_READ_IN_SIZE
						 : sizeof(*inarg);
		req.out_buf = kbuf;
		req.out_buf_max = chunk;

		err = fuse_request_send(fc, &req);
		if (err)
			break;
		if (req.out_buf_actual == 0)
			break;

		memcpy_tofs(buf, kbuf, req.out_buf_actual);
		file->f_pos += (loff_t)req.out_buf_actual;
		buf += req.out_buf_actual;
		count -= (int)req.out_buf_actual;
		total_read += (int)req.out_buf_actual;

		if (req.out_buf_actual < chunk)
			break;
	}

	kfree(kbuf);
	return total_read ? total_read : err;
}

static int fuse_file_write(struct inode *inode, struct file *file,
			   const char *buf, int count)
{
	struct fuse_conn *fc;
	struct fuse_file *ff;
	char *kbuf;
	unsigned int max_chunk;
	loff_t pos;
	int total_written = 0, err = 0;

	if (!inode)
		return -EINVAL;
	if (count <= 0)
		return 0;

	err = verify_area(VERIFY_READ, buf, count);
	if (err)
		return err;

	fc = fuse_get_conn(inode->i_sb);
	if (!fc)
		return -ENOTCONN;

	ff = (struct fuse_file *)file->private_data;
	if (file->f_flags & O_APPEND) {
		fuse_revalidate_stat(inode);
		pos = inode->i_size;
	} else {
		pos = file->f_pos;
	}

	max_chunk = FUSE_BOUNCE_SIZE;
	if (fc->max_write && fc->max_write < max_chunk)
		max_chunk = fc->max_write;

	kbuf = (char *)kmalloc(max_chunk, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	while (count > 0) {
		struct fuse_req req;
		struct fuse_write_in *inarg;
		struct fuse_write_out outarg;
		unsigned int chunk = (unsigned int)count;
		unsigned int written;

		if (chunk > max_chunk)
			chunk = max_chunk;

		memcpy_fromfs(kbuf, buf, chunk);

		fuse_req_init(&req);
		memset(&outarg, 0, sizeof(outarg));

		req.in.opcode = FUSE_WRITE;
		req.in.nodeid = fuse_get_nodeid(inode);

		inarg = (struct fuse_write_in *)req.in_arg;
		inarg->fh = ff ? ff->fh : 0;
		inarg->offset = (fuse_u64)pos;
		inarg->size = chunk;
		inarg->flags = file->f_flags;
		req.in_arg_len = (fc->minor < 9) ? FUSE_COMPAT_WRITE_IN_SIZE
						 : sizeof(*inarg);
		req.in_payload = kbuf;
		req.in_payload_len = chunk;
		req.out_buf = &outarg;
		req.out_buf_max = sizeof(outarg);

		err = fuse_request_send(fc, &req);
		if (err)
			break;
		if (req.out_buf_actual < sizeof(outarg)) {
			err = -EIO;
			break;
		}

		written = outarg.size;
		if (written > chunk)
			written = chunk;
		if (written == 0)
			break;

		pos += (loff_t)written;
		file->f_pos = pos;
		if (pos > inode->i_size)
			inode->i_size = pos;
		inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		inode->u.fuse_i.attr_valid = 0;

		buf += written;
		count -= (int)written;
		total_written += (int)written;

		if (written < chunk)
			break;
	}

	kfree(kbuf);
	return total_written ? total_written : err;
}

static int fuse_file_fsync(struct inode *inode, struct file *file)
{
	struct fuse_conn *fc = fuse_get_conn(inode->i_sb);
	struct fuse_file *ff = (struct fuse_file *)file->private_data;
	struct fuse_req req;
	struct fuse_fsync_in *inarg;
	int err;

	if (!fc)
		return -ENOTCONN;
	if (fc->no_fsync)
		return 0;

	fuse_req_init(&req);
	req.in.opcode = FUSE_FSYNC;
	req.in.nodeid = fuse_get_nodeid(inode);

	inarg = (struct fuse_fsync_in *)req.in_arg;
	inarg->fh = ff ? ff->fh : 0;
	inarg->fsync_flags = 0;
	req.in_arg_len = sizeof(*inarg);

	err = fuse_request_send(fc, &req);
	if (err == -ENOSYS) {
		fc->no_fsync = 1;
		err = 0;
	}
	return err;
}

static int fuse_file_lseek(struct inode *inode, struct file *file,
			   off_t offset, int origin)
{
	loff_t tmp;

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
		fuse_revalidate_stat(inode);
		tmp = inode->i_size + offset;
		break;
	default:
		return -EINVAL;
	}

	if (tmp < 0)
		return -EINVAL;
	file->f_pos = tmp;
	file->f_reada = 0;
	file->f_version = ++event;
	return (int)tmp;
}

static void fuse_file_truncate(struct inode *inode)
{
	if (inode)
		inode->u.fuse_i.attr_valid = 0;
}

static int fuse_readpage(struct inode *inode, struct page *page)
{
	struct fuse_conn *fc = fuse_get_conn(inode->i_sb);
	struct fuse_req req;
	struct fuse_read_in *inarg;
	unsigned long addr;
	int err;

	addr = page_address(page);
	page->count++;
	set_bit(PG_locked, &page->flags);
	memset((void *)addr, 0, PAGE_SIZE);

	if (fc) {
		fuse_req_init(&req);
		req.in.opcode = FUSE_READ;
		req.in.nodeid = fuse_get_nodeid(inode);

		inarg = (struct fuse_read_in *)req.in_arg;
		inarg->fh = 0;
		inarg->offset = (fuse_u64)page->offset;
		inarg->size = PAGE_SIZE;
		req.in_arg_len = (fc->minor < 9) ? FUSE_COMPAT_READ_IN_SIZE
						 : sizeof(*inarg);
		req.out_buf = (void *)addr;
		req.out_buf_max = PAGE_SIZE;

		err = fuse_request_send(fc, &req);
		if (!err)
			set_bit(PG_uptodate, &page->flags);
		else
			set_bit(PG_error, &page->flags);
	} else {
		set_bit(PG_error, &page->flags);
	}

	clear_bit(PG_locked, &page->flags);
	wake_up(&page->wait);
	free_page(addr);
	return 0;
}

static int fuse_bmap(struct inode *inode, int block)
{
	struct fuse_conn *fc = fuse_get_conn(inode->i_sb);
	struct fuse_req req;
	struct fuse_bmap_in *inarg;
	struct fuse_bmap_out outarg;
	int err;

	if (!fc || fc->no_bmap || block < 0)
		return 0;

	fuse_req_init(&req);
	memset(&outarg, 0, sizeof(outarg));

	req.in.opcode = FUSE_BMAP;
	req.in.nodeid = fuse_get_nodeid(inode);

	inarg = (struct fuse_bmap_in *)req.in_arg;
	inarg->block = (fuse_u64)block;
	inarg->blocksize = inode->i_sb->s_blocksize;
	req.in_arg_len = sizeof(*inarg);
	req.out_buf = &outarg;
	req.out_buf_max = sizeof(outarg);

	err = fuse_request_send(fc, &req);
	if (err == -ENOSYS) {
		fc->no_bmap = 1;
		return 0;
	}
	return err ? 0 : (int)outarg.block;
}

struct file_operations fuse_file_operations = {
	fuse_file_lseek,	/* lseek */
	fuse_file_read,		/* read */
	fuse_file_write,	/* write */
	NULL,			/* readdir */
	NULL,			/* select */
	NULL,			/* ioctl */
	generic_file_mmap,	/* mmap */
	fuse_file_open,		/* open */
	fuse_file_release,	/* release */
	fuse_file_fsync,	/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL			/* revalidate */
};

struct inode_operations fuse_file_inode_operations = {
	&fuse_file_operations,	/* default_file_ops */
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
	fuse_readpage,		/* readpage */
	NULL,			/* writepage */
	fuse_bmap,		/* bmap */
	fuse_file_truncate,	/* truncate */
	fuse_permission,	/* permission */
	NULL			/* smap */
};
