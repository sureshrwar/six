/*
 * linux/fs/fuse/fuse_i.h
 *
 * Internal declarations for the SIX (Linux 2.0.11) FUSE filesystem and
 * /dev/fuse character device.
 */

#ifndef _FS_FUSE_I_H
#define _FS_FUSE_I_H

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/fuse.h>

#define FUSE_DEFAULT_MAX_READ	65536
#define FUSE_DEFAULT_MAX_WRITE	65536
#define FUSE_DEFAULT_BLKSIZE	4096

enum fuse_req_state {
	FUSE_REQ_INIT = 0,
	FUSE_REQ_PENDING,
	FUSE_REQ_SENT,
	FUSE_REQ_FINISHED
};

struct fuse_forget_link {
	fuse_u64		nodeid;
	fuse_u64		nlookup;
	struct fuse_forget_link	*next;
};

/*
 * Every request/reply payload is held in kernel memory (SOLARIS_RAM_START)
 * rather than referencing a guest task's user VMA directly.  In SIX, all
 * guest binaries share SIX_BIND_POINT (0x3000000) and switch_to() remaps
 * that range on every context switch, so /dev/fuse read/write in the daemon
 * process must copy to/from kernel buffers.
 */
struct fuse_req {
	struct fuse_in_header	in;
	unsigned char		in_arg[128];
	unsigned int		in_arg_len;
	const void		*in_payload;
	unsigned int		in_payload_len;

	struct fuse_out_header	out;
	void			*out_buf;
	unsigned int		out_buf_max;
	unsigned int		out_buf_actual;

	enum fuse_req_state	state;
	int			is_async;
	void			(*async_end)(struct fuse_conn *, struct fuse_req *);
	struct wait_queue	*waitq;
	struct fuse_req		*next;
};

struct fuse_file {
	fuse_u64		fh;
	fuse_u32		open_flags;
	int			is_dir;
};

struct fuse_conn {
	int			refcount;
	int			mounted;
	int			connected;
	int			initialized;
	int			init_error;

	fuse_u32		major;
	fuse_u32		minor;
	fuse_u32		max_read;
	fuse_u32		max_write;
	fuse_u32		init_flags;
	fuse_u32		blksize;

	uid_t			user_id;
	gid_t			group_id;
	umode_t			rootmode;
	int			default_permissions;
	int			allow_other;

	fuse_u64		last_unique;

	struct fuse_req		*pending_head;
	struct fuse_req		*pending_tail;
	struct fuse_req		*processing_head;

	struct fuse_forget_link	*forget_head;
	struct fuse_forget_link	*forget_tail;

	struct wait_queue	*waitq;		/* /dev/fuse readers */
	struct wait_queue	*init_waitq;	/* VFS callers awaiting INIT */

	/* Memoized -ENOSYS flags for optional opcodes */
	unsigned char		no_create;
	unsigned char		no_open;
	unsigned char		no_opendir;
	unsigned char		no_releasedir;
	unsigned char		no_flush;
	unsigned char		no_fsync;
	unsigned char		no_fsyncdir;
	unsigned char		no_access;
	unsigned char		no_statfs;
	unsigned char		no_setxattr;
	unsigned char		no_getxattr;
	unsigned char		no_listxattr;
	unsigned char		no_removexattr;
	unsigned char		no_bmap;
	unsigned char		no_destroy;
};

static inline struct fuse_conn *fuse_get_conn(struct super_block *sb)
{
	return sb ? sb->u.fuse_sb.fc : NULL;
}

static inline fuse_u64 fuse_get_nodeid(struct inode *inode)
{
	return inode->u.fuse_i.nodeid ? inode->u.fuse_i.nodeid : (fuse_u64)inode->i_ino;
}

static inline unsigned int fuse_attr_size(struct fuse_conn *fc)
{
	return (fc->minor < 9) ? FUSE_COMPAT_ATTR_SIZE : sizeof(struct fuse_attr);
}

static inline unsigned int fuse_entry_out_size(struct fuse_conn *fc)
{
	return (fc->minor < 9) ? FUSE_COMPAT_ENTRY_OUT_SIZE : sizeof(struct fuse_entry_out);
}

static inline unsigned int fuse_attr_out_size(struct fuse_conn *fc)
{
	return (fc->minor < 9) ? FUSE_COMPAT_ATTR_OUT_SIZE : sizeof(struct fuse_attr_out);
}

/* dev.c */
extern struct file_operations fuse_dev_fops;
extern int fuse_dev_writev(struct inode *inode, struct file *file,
			   const struct iovec *iov, unsigned long iov_count,
			   unsigned int count);
extern struct fuse_conn *fuse_conn_get(struct fuse_conn *fc);
extern void fuse_conn_put(struct fuse_conn *fc);
extern void fuse_conn_abort(struct fuse_conn *fc);
extern struct fuse_conn *fuse_conn_from_fd(unsigned int fd);
extern void fuse_send_init(struct fuse_conn *fc);
extern void fuse_queue_forget(struct fuse_conn *fc, fuse_u64 nodeid, fuse_u64 nlookup);
extern void fuse_req_init(struct fuse_req *req);
extern int fuse_request_send(struct fuse_conn *fc, struct fuse_req *req);

/* inode.c */
extern struct inode_operations fuse_dir_inode_operations;
extern struct inode_operations fuse_file_inode_operations;
extern struct inode_operations fuse_symlink_inode_operations;
extern struct file_operations fuse_dir_operations;
extern struct file_operations fuse_file_operations;

extern void fuse_change_attributes(struct inode *inode, struct fuse_attr *attr,
				   fuse_u64 attr_valid);
extern struct inode *fuse_iget(struct super_block *sb, struct fuse_entry_out *entry);
extern int fuse_do_getattr(struct inode *inode);
extern int fuse_permission(struct inode *inode, int mask);

#endif /* _FS_FUSE_I_H */
