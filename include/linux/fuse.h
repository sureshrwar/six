/*
 * linux/include/linux/fuse.h
 *
 * Filesystem in Userspace (FUSE) wire protocol (ABI 7.x) and kernel
 * declarations for SIX (Linux 2.0.11).
 *
 * The wire structures here match the upstream Linux FUSE 7.x kernel ABI
 * byte-for-byte so that unmodified 7.x FUSE daemons (libfuse 2.x/3.x,
 * ntfs-3g, etc.) can communicate over /dev/fuse.
 */

#ifndef _LINUX_FUSE_H
#define _LINUX_FUSE_H

#include <linux/types.h>

/*
 * Version negotiation:
 * We advertise 7.23 and accept any 7.x daemon (7.1 through 7.x).
 * Struct size differences across 7.x minor versions (e.g. 7.1..7.8
 * 80-byte fuse_attr vs 7.9+ 88-byte fuse_attr, 24-byte vs 40-byte
 * fuse_write_in, 8-byte vs 16-byte fuse_mknod_in / fuse_create_in)
 * are adapted dynamically based on the daemon's negotiated minor
 * version and reply length.
 */
#define FUSE_KERNEL_VERSION		7
#define FUSE_KERNEL_MINOR_VERSION	23

#define FUSE_ROOT_ID			1ULL

#define FUSE_SUPER_MAGIC		0x65735546UL
#define FUSE_MINOR			229

/* Minimum read buffer size required by the FUSE device */
#define FUSE_MIN_READ_BUFFER		8192

/*
 * FUSE wire types (explicit 32/64-bit widths).
 * In 32-bit x86 GCC, unsigned long long is 64-bit.
 */
typedef unsigned char		fuse_u8;
typedef unsigned short		fuse_u16;
typedef unsigned int		fuse_u32;
typedef signed int		fuse_s32;
typedef unsigned long long	fuse_u64;
typedef signed long long	fuse_s64;

struct fuse_attr {
	fuse_u64	ino;
	fuse_u64	size;
	fuse_u64	blocks;
	fuse_u64	atime;
	fuse_u64	mtime;
	fuse_u64	ctime;
	fuse_u32	atimensec;
	fuse_u32	mtimensec;
	fuse_u32	ctimensec;
	fuse_u32	mode;
	fuse_u32	nlink;
	fuse_u32	uid;
	fuse_u32	gid;
	fuse_u32	rdev;
	fuse_u32	blksize;
	fuse_u32	padding;
};

/* Size of fuse_attr prior to ABI 7.9 (without blksize & padding) */
#define FUSE_COMPAT_ATTR_SIZE		80

struct fuse_kstatfs {
	fuse_u64	blocks;
	fuse_u64	bfree;
	fuse_u64	bavail;
	fuse_u64	files;
	fuse_u64	ffree;
	fuse_u32	bsize;
	fuse_u32	namelen;
	fuse_u32	frsize;
	fuse_u32	padding;
	fuse_u32	spare[6];
};

struct fuse_file_lock {
	fuse_u64	start;
	fuse_u64	end;
	fuse_u32	type;
	fuse_u32	pid;
};

/*
 * Bitmasks for fuse_setattr_in.valid
 */
#define FATTR_MODE		(1 << 0)
#define FATTR_UID		(1 << 1)
#define FATTR_GID		(1 << 2)
#define FATTR_SIZE		(1 << 3)
#define FATTR_ATIME		(1 << 4)
#define FATTR_MTIME		(1 << 5)
#define FATTR_FH		(1 << 6)
#define FATTR_ATIME_NOW		(1 << 7)
#define FATTR_MTIME_NOW		(1 << 8)
#define FATTR_LOCKOWNER		(1 << 9)
#define FATTR_CTIME		(1 << 10)

/*
 * Flags returned by the OPEN request
 */
#define FOPEN_DIRECT_IO		(1 << 0)
#define FOPEN_KEEP_CACHE	(1 << 1)
#define FOPEN_NONSEEKABLE	(1 << 2)

/*
 * INIT request/reply flags
 */
#define FUSE_ASYNC_READ		(1 << 0)
#define FUSE_POSIX_LOCKS	(1 << 1)
#define FUSE_FILE_OPS		(1 << 2)
#define FUSE_ATOMIC_O_TRUNC	(1 << 3)
#define FUSE_EXPORT_SUPPORT	(1 << 4)
#define FUSE_BIG_WRITES		(1 << 5)
#define FUSE_DONT_MASK		(1 << 6)
#define FUSE_SPLICE_WRITE	(1 << 7)
#define FUSE_SPLICE_MOVE	(1 << 8)
#define FUSE_SPLICE_READ	(1 << 9)
#define FUSE_FLOCK_LOCKS	(1 << 10)
#define FUSE_HAS_IOCTL_DIR	(1 << 11)
#define FUSE_AUTO_INVAL_DATA	(1 << 12)
#define FUSE_DO_READDIRPLUS	(1 << 13)
#define FUSE_READDIRPLUS_AUTO	(1 << 14)
#define FUSE_ASYNC_DIO		(1 << 15)
#define FUSE_WRITEBACK_CACHE	(1 << 16)
#define FUSE_NO_OPEN_SUPPORT	(1 << 17)

/*
 * Release flags
 */
#define FUSE_RELEASE_FLUSH	(1 << 0)
#define FUSE_RELEASE_FLOCK_UNLOCK (1 << 1)

/*
 * Getattr flags
 */
#define FUSE_GETATTR_FH		(1 << 0)

/*
 * Write flags
 */
#define FUSE_WRITE_CACHE	(1 << 0)
#define FUSE_WRITE_LOCKOWNER	(1 << 1)

/*
 * FUSE opcodes
 */
enum fuse_opcode {
	FUSE_LOOKUP		= 1,
	FUSE_FORGET		= 2,	/* no reply */
	FUSE_GETATTR		= 3,
	FUSE_SETATTR		= 4,
	FUSE_READLINK		= 5,
	FUSE_SYMLINK		= 6,
	FUSE_MKNOD		= 8,
	FUSE_MKDIR		= 9,
	FUSE_UNLINK		= 10,
	FUSE_RMDIR		= 11,
	FUSE_RENAME		= 12,
	FUSE_LINK		= 13,
	FUSE_OPEN		= 14,
	FUSE_READ		= 15,
	FUSE_WRITE		= 16,
	FUSE_STATFS		= 17,
	FUSE_RELEASE		= 18,
	FUSE_FSYNC		= 20,
	FUSE_SETXATTR		= 21,
	FUSE_GETXATTR		= 22,
	FUSE_LISTXATTR		= 23,
	FUSE_REMOVEXATTR	= 24,
	FUSE_FLUSH		= 25,
	FUSE_INIT		= 26,
	FUSE_OPENDIR		= 27,
	FUSE_READDIR		= 28,
	FUSE_RELEASEDIR		= 29,
	FUSE_FSYNCDIR		= 30,
	FUSE_GETLK		= 31,
	FUSE_SETLK		= 32,
	FUSE_SETLKW		= 33,
	FUSE_ACCESS		= 34,
	FUSE_CREATE		= 35,
	FUSE_INTERRUPT		= 36,
	FUSE_BMAP		= 37,
	FUSE_DESTROY		= 38,
	FUSE_IOCTL		= 39,
	FUSE_POLL		= 40,
	FUSE_NOTIFY_REPLY	= 41,
	FUSE_BATCH_FORGET	= 42,
	FUSE_FALLOCATE		= 43,
	FUSE_READDIRPLUS	= 44
};

struct fuse_entry_out {
	fuse_u64	nodeid;
	fuse_u64	generation;
	fuse_u64	entry_valid;
	fuse_u64	attr_valid;
	fuse_u32	entry_valid_nsec;
	fuse_u32	attr_valid_nsec;
	struct fuse_attr attr;
};

#define FUSE_COMPAT_ENTRY_OUT_SIZE	128

struct fuse_forget_in {
	fuse_u64	nlookup;
};

struct fuse_forget_one {
	fuse_u64	nodeid;
	fuse_u64	nlookup;
};

struct fuse_batch_forget_in {
	fuse_u32	count;
	fuse_u32	dummy;
};

struct fuse_getattr_in {
	fuse_u32	getattr_flags;
	fuse_u32	dummy;
	fuse_u64	fh;
};

struct fuse_attr_out {
	fuse_u64	attr_valid;
	fuse_u32	attr_valid_nsec;
	fuse_u32	dummy;
	struct fuse_attr attr;
};

#define FUSE_COMPAT_ATTR_OUT_SIZE	96

struct fuse_mknod_in {
	fuse_u32	mode;
	fuse_u32	rdev;
	fuse_u32	umask;
	fuse_u32	padding;
};

#define FUSE_COMPAT_MKNOD_IN_SIZE	8

struct fuse_mkdir_in {
	fuse_u32	mode;
	fuse_u32	umask;
};

struct fuse_rename_in {
	fuse_u64	newdir;
};

struct fuse_link_in {
	fuse_u64	oldnodeid;
};

struct fuse_setattr_in {
	fuse_u32	valid;
	fuse_u32	padding;
	fuse_u64	fh;
	fuse_u64	size;
	fuse_u64	lock_owner;
	fuse_u64	atime;
	fuse_u64	mtime;
	fuse_u64	ctime;
	fuse_u32	atimensec;
	fuse_u32	mtimensec;
	fuse_u32	ctimensec;
	fuse_u32	mode;
	fuse_u32	unused4;
	fuse_u32	uid;
	fuse_u32	gid;
	fuse_u32	unused5;
};

struct fuse_open_in {
	fuse_u32	flags;
	fuse_u32	unused;
};

struct fuse_create_in {
	fuse_u32	flags;
	fuse_u32	mode;
	fuse_u32	umask;
	fuse_u32	padding;
};

#define FUSE_COMPAT_CREATE_IN_SIZE	8

struct fuse_open_out {
	fuse_u64	fh;
	fuse_u32	open_flags;
	fuse_u32	padding;
};

struct fuse_release_in {
	fuse_u64	fh;
	fuse_u32	flags;
	fuse_u32	release_flags;
	fuse_u64	lock_owner;
};

struct fuse_flush_in {
	fuse_u64	fh;
	fuse_u32	unused;
	fuse_u32	padding;
	fuse_u64	lock_owner;
};

struct fuse_read_in {
	fuse_u64	fh;
	fuse_u64	offset;
	fuse_u32	size;
	fuse_u32	read_flags;
	fuse_u64	lock_owner;
	fuse_u32	flags;
	fuse_u32	padding;
};

#define FUSE_COMPAT_READ_IN_SIZE	24

struct fuse_write_in {
	fuse_u64	fh;
	fuse_u64	offset;
	fuse_u32	size;
	fuse_u32	write_flags;
	fuse_u64	lock_owner;
	fuse_u32	flags;
	fuse_u32	padding;
};

#define FUSE_COMPAT_WRITE_IN_SIZE	24

struct fuse_write_out {
	fuse_u32	size;
	fuse_u32	padding;
};

struct fuse_statfs_out {
	struct fuse_kstatfs st;
};

struct fuse_fsync_in {
	fuse_u64	fh;
	fuse_u32	fsync_flags;
	fuse_u32	padding;
};

struct fuse_setxattr_in {
	fuse_u32	size;
	fuse_u32	flags;
};

struct fuse_getxattr_in {
	fuse_u32	size;
	fuse_u32	padding;
};

struct fuse_getxattr_out {
	fuse_u32	size;
	fuse_u32	padding;
};

struct fuse_lk_in {
	fuse_u64	fh;
	fuse_u64	owner;
	struct fuse_file_lock lk;
	fuse_u32	lk_flags;
	fuse_u32	padding;
};

struct fuse_lk_out {
	struct fuse_file_lock lk;
};

struct fuse_access_in {
	fuse_u32	mask;
	fuse_u32	padding;
};

struct fuse_init_in {
	fuse_u32	major;
	fuse_u32	minor;
	fuse_u32	max_readahead;
	fuse_u32	flags;
};

struct fuse_init_out {
	fuse_u32	major;
	fuse_u32	minor;
	fuse_u32	max_readahead;
	fuse_u32	flags;
	fuse_u16	max_background;
	fuse_u16	congestion_threshold;
	fuse_u32	max_write;
	fuse_u32	time_gran;
	fuse_u32	unused[9];
};

struct fuse_interrupt_in {
	fuse_u64	unique;
};

struct fuse_bmap_in {
	fuse_u64	block;
	fuse_u32	blocksize;
	fuse_u32	padding;
};

struct fuse_bmap_out {
	fuse_u64	block;
};

struct fuse_in_header {
	fuse_u32	len;
	fuse_u32	opcode;
	fuse_u64	unique;
	fuse_u64	nodeid;
	fuse_u32	uid;
	fuse_u32	gid;
	fuse_u32	pid;
	fuse_u32	padding;
};

struct fuse_out_header {
	fuse_u32	len;
	fuse_s32	error;
	fuse_u64	unique;
};

struct fuse_dirent {
	fuse_u64	ino;
	fuse_u64	off;
	fuse_u32	namelen;
	fuse_u32	type;
	char		name[0];
};

#define FUSE_NAME_OFFSET	24
#define FUSE_DIRENT_ALIGN(x)	(((x) + sizeof(fuse_u64) - 1) & ~(sizeof(fuse_u64) - 1))
#define FUSE_DIRENT_SIZE(d)	FUSE_DIRENT_ALIGN(FUSE_NAME_OFFSET + (d)->namelen)

#ifdef __KERNEL__

struct fuse_conn;

struct fuse_inode_info {
	fuse_u64	nodeid;
	fuse_u64	generation;
	fuse_u64	nlookup;
	unsigned long	attr_valid;	/* jiffies deadline */
	/*
	 * Stashed handle from FUSE_CREATE so the immediately-following
	 * do_open() -> fuse_open() can adopt it without issuing a second
	 * FUSE_OPEN over the wire.
	 */
	int		has_create_fh;
	fuse_u64	create_fh;
	fuse_u32	create_open_flags;
};

struct fuse_sb_info {
	struct fuse_conn *fc;
};

extern int init_fuse_fs(void);
extern int fuse_revalidate_stat(struct inode *inode);

#endif /* __KERNEL__ */

#endif /* _LINUX_FUSE_H */
