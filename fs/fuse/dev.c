/*
 * linux/fs/fuse/dev.c
 *
 * /dev/fuse character device (major 10, minor 229) and request/reply queue
 * engine for SIX (Linux 2.0.11).
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/major.h>
#include <asm/segment.h>
#include <asm/system.h>

#include "fuse_i.h"

struct fuse_conn *fuse_conn_get(struct fuse_conn *fc)
{
	if (fc)
		fc->refcount++;
	return fc;
}

void fuse_conn_put(struct fuse_conn *fc)
{
	struct fuse_forget_link *fl;

	if (!fc)
		return;
	if (--fc->refcount > 0)
		return;

	while ((fl = fc->forget_head) != NULL) {
		fc->forget_head = fl->next;
		kfree(fl);
	}
	kfree(fc);
}

/*
 * Abort all pending and in-flight requests on a connection (e.g. when the
 * daemon closes /dev/fuse or the filesystem is unmounted) so no VFS caller
 * can ever hang waiting for a dead userspace daemon.
 */
void fuse_conn_abort(struct fuse_conn *fc)
{
	struct fuse_req *req, *next;

	if (!fc)
		return;

	fc->connected = 0;

	req = fc->pending_head;
	fc->pending_head = fc->pending_tail = NULL;
	while (req) {
		next = req->next;
		req->next = NULL;
		req->out.error = -ENOTCONN;
		req->state = FUSE_REQ_FINISHED;
		if (req->is_async && req->async_end)
			req->async_end(fc, req);
		else
			wake_up(&req->waitq);
		req = next;
	}

	req = fc->processing_head;
	fc->processing_head = NULL;
	while (req) {
		next = req->next;
		req->next = NULL;
		req->out.error = -ENOTCONN;
		req->state = FUSE_REQ_FINISHED;
		if (req->is_async && req->async_end)
			req->async_end(fc, req);
		else
			wake_up(&req->waitq);
		req = next;
	}

	wake_up_interruptible(&fc->waitq);
	wake_up(&fc->init_waitq);
}

void fuse_req_init(struct fuse_req *req)
{
	memset(req, 0, sizeof(*req));
	req->state = FUSE_REQ_INIT;
}

static void fuse_enqueue_req(struct fuse_conn *fc, struct fuse_req *req, int at_head)
{
	req->state = FUSE_REQ_PENDING;
	req->next = NULL;

	if (at_head) {
		req->next = fc->pending_head;
		fc->pending_head = req;
		if (!fc->pending_tail)
			fc->pending_tail = req;
	} else {
		if (fc->pending_tail)
			fc->pending_tail->next = req;
		else
			fc->pending_head = req;
		fc->pending_tail = req;
	}

	wake_up_interruptible(&fc->waitq);
}

static void fuse_process_init_reply(struct fuse_conn *fc, struct fuse_req *req)
{
	struct fuse_init_out *arg = (struct fuse_init_out *)req->out_buf;

	if (req->out.error != 0 || req->out_buf_actual < 24) {
		fc->init_error = req->out.error ? req->out.error : -EPROTO;
	} else if (arg->major != FUSE_KERNEL_VERSION) {
		printk("FUSE: unsupported daemon protocol version %u.%u\n",
		       arg->major, arg->minor);
		fc->init_error = -EPROTO;
	} else {
		fc->major = arg->major;
		fc->minor = arg->minor;
		fc->init_flags = arg->flags;
		if (arg->max_write >= 4096 && arg->max_write < fc->max_write)
			fc->max_write = arg->max_write;
		if (arg->flags & FUSE_NO_OPEN_SUPPORT)
			fc->no_open = 1;
		fc->initialized = 1;
	}

	if (req->out_buf)
		kfree(req->out_buf);
	kfree(req);
	wake_up(&fc->init_waitq);
}

/*
 * Queue FUSE_INIT asynchronously during mount(2).
 *
 * A single-threaded FUSE daemon (such as ntfs-3g or a standard libfuse
 * program before daemonizing) opens /dev/fuse, calls mount(2), and only
 * enters its read(/dev/fuse) loop *after* mount(2) returns.  Queueing
 * FUSE_INIT asynchronously allows mount(2) to return 0 immediately while
 * ensuring FUSE_INIT is the very first message read from /dev/fuse, and
 * any subsequent VFS operation waits on fc->init_waitq until the reply
 * arrives.
 */
void fuse_send_init(struct fuse_conn *fc)
{
	struct fuse_req *req;
	struct fuse_init_in *inarg;
	struct fuse_init_out *outarg;

	req = (struct fuse_req *)kmalloc(sizeof(*req), GFP_KERNEL);
	if (!req) {
		fc->init_error = -ENOMEM;
		return;
	}
	outarg = (struct fuse_init_out *)kmalloc(sizeof(*outarg), GFP_KERNEL);
	if (!outarg) {
		kfree(req);
		fc->init_error = -ENOMEM;
		return;
	}

	fuse_req_init(req);
	memset(outarg, 0, sizeof(*outarg));

	req->in.opcode = FUSE_INIT;
	req->in.nodeid = 0;
	req->in.unique = ++fc->last_unique;
	req->in.uid = current->fsuid;
	req->in.gid = current->fsgid;
	req->in.pid = current->pid;

	inarg = (struct fuse_init_in *)req->in_arg;
	inarg->major = FUSE_KERNEL_VERSION;
	inarg->minor = FUSE_KERNEL_MINOR_VERSION;
	inarg->max_readahead = FUSE_DEFAULT_MAX_READ;
	inarg->flags = FUSE_ASYNC_READ | FUSE_POSIX_LOCKS | FUSE_FILE_OPS |
		       FUSE_ATOMIC_O_TRUNC | FUSE_BIG_WRITES | FUSE_DONT_MASK;
	req->in_arg_len = sizeof(*inarg);
	req->in.len = sizeof(struct fuse_in_header) + req->in_arg_len;

	req->out_buf = outarg;
	req->out_buf_max = sizeof(*outarg);
	req->is_async = 1;
	req->async_end = fuse_process_init_reply;

	fuse_enqueue_req(fc, req, 1);
}

void fuse_queue_forget(struct fuse_conn *fc, fuse_u64 nodeid, fuse_u64 nlookup)
{
	struct fuse_forget_link *fl;

	if (!fc || !fc->connected || !nlookup || nodeid == FUSE_ROOT_ID)
		return;

	fl = (struct fuse_forget_link *)kmalloc(sizeof(*fl), GFP_KERNEL);
	if (!fl)
		return;

	fl->nodeid = nodeid;
	fl->nlookup = nlookup;
	fl->next = NULL;

	if (fc->forget_tail)
		fc->forget_tail->next = fl;
	else
		fc->forget_head = fl;
	fc->forget_tail = fl;

	wake_up_interruptible(&fc->waitq);
}

int fuse_request_send(struct fuse_conn *fc, struct fuse_req *req)
{
	if (!fc || !fc->connected)
		return -ENOTCONN;

	if (req->in.opcode != FUSE_INIT) {
		while (!fc->initialized && !fc->init_error && fc->connected)
			sleep_on(&fc->init_waitq);
		if (!fc->connected)
			return -ENOTCONN;
		if (fc->init_error)
			return fc->init_error;
	}

	req->in.unique = ++fc->last_unique;
	req->in.uid = current->fsuid;
	req->in.gid = current->fsgid;
	req->in.pid = current->pid;
	req->in.len = sizeof(struct fuse_in_header) + req->in_arg_len + req->in_payload_len;

	fuse_enqueue_req(fc, req, 0);

	while (req->state != FUSE_REQ_FINISHED && fc->connected)
		sleep_on(&req->waitq);

	if (req->state != FUSE_REQ_FINISHED)
		return -ENOTCONN;

	return req->out.error;
}

static int fuse_dev_open(struct inode *inode, struct file *file)
{
	struct fuse_conn *fc;

	if (MINOR(inode->i_rdev) != FUSE_MINOR)
		return -ENODEV;

	fc = (struct fuse_conn *)kmalloc(sizeof(*fc), GFP_KERNEL);
	if (!fc)
		return -ENOMEM;

	memset(fc, 0, sizeof(*fc));
	fc->refcount = 1;
	fc->connected = 1;
	fc->major = FUSE_KERNEL_VERSION;
	fc->minor = FUSE_KERNEL_MINOR_VERSION;
	fc->max_read = FUSE_DEFAULT_MAX_READ;
	fc->max_write = FUSE_DEFAULT_MAX_WRITE;
	fc->blksize = FUSE_DEFAULT_BLKSIZE;

	file->private_data = fc;
	return 0;
}

static void fuse_dev_release(struct inode *inode, struct file *file)
{
	struct fuse_conn *fc = (struct fuse_conn *)file->private_data;

	if (fc) {
		fuse_conn_abort(fc);
		fuse_conn_put(fc);
		file->private_data = NULL;
	}
}

static int fuse_dev_read(struct inode *inode, struct file *file,
			 char *buf, int count)
{
	struct fuse_conn *fc = (struct fuse_conn *)file->private_data;
	struct fuse_req *req;
	struct fuse_forget_link *fl;
	unsigned int total;
	int err;

	if (!fc)
		return -ENODEV;
	if (count < (int)sizeof(struct fuse_in_header))
		return -EINVAL;

	err = verify_area(VERIFY_WRITE, buf, count);
	if (err)
		return err;

	while (fc->connected && !fc->pending_head && !fc->forget_head) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (current->signal & ~current->blocked)
			return -ERESTARTSYS;
		interruptible_sleep_on(&fc->waitq);
	}

	if (!fc->connected)
		return -ENODEV;

	if (fc->pending_head) {
		req = fc->pending_head;
		total = sizeof(struct fuse_in_header) + req->in_arg_len + req->in_payload_len;
		if ((unsigned int)count < total)
			return -EINVAL;

		fc->pending_head = req->next;
		if (!fc->pending_head)
			fc->pending_tail = NULL;

		req->in.len = total;
		memcpy_tofs(buf, &req->in, sizeof(req->in));
		if (req->in_arg_len)
			memcpy_tofs(buf + sizeof(req->in), req->in_arg, req->in_arg_len);
		if (req->in_payload_len && req->in_payload)
			memcpy_tofs(buf + sizeof(req->in) + req->in_arg_len,
				    req->in_payload, req->in_payload_len);

		req->state = FUSE_REQ_SENT;
		req->next = fc->processing_head;
		fc->processing_head = req;
		return (int)total;
	}

	if (fc->forget_head) {
		struct fuse_in_header hdr;
		struct fuse_forget_in fin;

		total = sizeof(hdr) + sizeof(fin);
		if ((unsigned int)count < total)
			return -EINVAL;

		fl = fc->forget_head;
		fc->forget_head = fl->next;
		if (!fc->forget_head)
			fc->forget_tail = NULL;

		memset(&hdr, 0, sizeof(hdr));
		hdr.len = total;
		hdr.opcode = FUSE_FORGET;
		hdr.unique = ++fc->last_unique;
		hdr.nodeid = fl->nodeid;
		fin.nlookup = fl->nlookup;
		kfree(fl);

		memcpy_tofs(buf, &hdr, sizeof(hdr));
		memcpy_tofs(buf + sizeof(hdr), &fin, sizeof(fin));
		return (int)total;
	}

	return 0;
}

static int fuse_dev_write(struct inode *inode, struct file *file,
			  const char *buf, int count)
{
	struct fuse_conn *fc = (struct fuse_conn *)file->private_data;
	struct fuse_out_header oh;
	struct fuse_req *req, **prev;
	unsigned int payload_len;
	int err;

	if (!fc || !fc->connected)
		return -ENODEV;
	if (count < (int)sizeof(struct fuse_out_header))
		return -EINVAL;

	err = verify_area(VERIFY_READ, buf, count);
	if (err)
		return err;

	memcpy_fromfs(&oh, buf, sizeof(oh));
	if (oh.len != (fuse_u32)count)
		return -EINVAL;

	/*
	 * unique == 0 is a daemon-initiated notification (FUSE_NOTIFY_*).
	 * Accept it cleanly so cache-invalidation calls succeed.
	 */
	if (oh.unique == 0)
		return count;

	prev = &fc->processing_head;
	req = NULL;
	while (*prev) {
		if ((*prev)->in.unique == oh.unique) {
			req = *prev;
			*prev = req->next;
			req->next = NULL;
			break;
		}
		prev = &((*prev)->next);
	}

	if (!req)
		return -ENOENT;

	/* Normalize positive error numbers if a daemon forgot the minus sign */
	if (oh.error > 0)
		oh.error = -oh.error;

	req->out = oh;
	payload_len = (unsigned int)count - sizeof(struct fuse_out_header);
	req->out_buf_actual = 0;

	if (oh.error == 0 && payload_len > 0 && req->out_buf) {
		if (payload_len > req->out_buf_max)
			payload_len = req->out_buf_max;
		memcpy_fromfs(req->out_buf, buf + sizeof(struct fuse_out_header),
			      payload_len);
		req->out_buf_actual = payload_len;
	}

	req->state = FUSE_REQ_FINISHED;
	if (req->is_async && req->async_end)
		req->async_end(fc, req);
	else
		wake_up(&req->waitq);

	return count;
}

static int fuse_dev_select(struct inode *inode, struct file *file,
			   int sel_type, select_table *wait)
{
	struct fuse_conn *fc = (struct fuse_conn *)file->private_data;

	if (!fc)
		return 0;

	switch (sel_type) {
	case SEL_IN:
		if (!fc->connected || fc->pending_head || fc->forget_head)
			return 1;
		select_wait(&fc->waitq, wait);
		return 0;
	case SEL_OUT:
		return 1;
	case SEL_EX:
		if (!fc->connected)
			return 1;
		select_wait(&fc->waitq, wait);
		return 0;
	}
	return 0;
}

struct file_operations fuse_dev_fops = {
	NULL,			/* lseek */
	fuse_dev_read,		/* read */
	fuse_dev_write,		/* write */
	NULL,			/* readdir */
	fuse_dev_select,	/* select */
	NULL,			/* ioctl */
	NULL,			/* mmap */
	fuse_dev_open,		/* open */
	fuse_dev_release,	/* release */
	NULL,			/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL			/* revalidate */
};

struct fuse_conn *fuse_conn_from_fd(unsigned int fd)
{
	struct file *file;

	if (fd >= NR_OPEN)
		return NULL;
	file = current->files->fd[fd];
	if (!file || file->f_op != &fuse_dev_fops || !file->private_data)
		return NULL;
	return (struct fuse_conn *)file->private_data;
}
