/*
 * include/linux/erofs_fs.h
 *
 * Enhanced Read-Only File System (EROFS) v1 on-disk structures and VFS
 * declarations for SIX.
 */
#ifndef _LINUX_EROFS_FS_H
#define _LINUX_EROFS_FS_H

#include <linux/types.h>

#define EROFS_SUPER_MAGIC_V1	0xE0F5E1E2UL
#define EROFS_SUPER_OFFSET	1024

#define EROFS_BLKSIZ		1024
#define EROFS_BLKSZBITS		10
#define EROFS_ISLOTBITS		5
#define EROFS_SLOTSIZE		(1U << EROFS_ISLOTBITS) /* 32 bytes */

/* Inode i_format bitfields */
#define EROFS_I_VERSION_MASK	0x01
#define EROFS_I_VERS_COMPACT	0
#define EROFS_I_VERS_EXTENDED	1

#define EROFS_I_DATALAYOUT_BIT	1
#define EROFS_I_DATALAYOUT_MASK	0x07

#define EROFS_INODE_FLAT_PLAIN		0
#define EROFS_INODE_COMPRESSED_FULL	1
#define EROFS_INODE_FLAT_INLINE		2
#define EROFS_INODE_COMPRESSED_COMPACT	3
#define EROFS_INODE_CHUNK_BASED		4

/* Directory entry file_type values */
#define EROFS_FT_UNKNOWN	0
#define EROFS_FT_REG_FILE	1
#define EROFS_FT_DIR		2
#define EROFS_FT_CHRDEV		3
#define EROFS_FT_BLKDEV		4
#define EROFS_FT_FIFO		5
#define EROFS_FT_SOCK		6
#define EROFS_FT_SYMLINK	7
#define EROFS_FT_MAX		8

#define EROFS_NAME_LEN		255

/*
 * EROFS v1 on-disk superblock (128 bytes at byte offset 1024).
 */
struct erofs_super_block {
	__u32 magic;		/* 0x00: 0xE0F5E1E2 */
	__u32 checksum;		/* 0x04: crc32c(super_block) */
	__u32 feature_compat;	/* 0x08 */
	__u8  blkszbits;	/* 0x0C: filesystem block size bits (10 -> 1024) */
	__u8  extslots;		/* 0x0D: superblock extension slots */
	__u16 root_nid;		/* 0x0E: nid of root directory */
	__u64 inos;		/* 0x10: total valid ino count */
	__u64 build_time;	/* 0x18: inode v1 time derivation */
	__u32 build_time_nsec;	/* 0x20 */
	__u32 blocks;		/* 0x24: total block count */
	__u32 meta_blkaddr;	/* 0x28: start block address of metadata area */
	__u32 xattr_blkaddr;	/* 0x2C: start block address of shared xattr area */
	__u8  uuid[16];		/* 0x30: 128-bit uuid for volume */
	__u8  volume_name[16];	/* 0x40: volume name */
	__u32 feature_incompat;	/* 0x50 */
	__u16 compr_algs;	/* 0x54 */
	__u16 extra_devices;	/* 0x56 */
	__u16 devt_slotoff;	/* 0x58 */
	__u8  dirblkbits;	/* 0x5A */
	__u8  xattr_prefix_count; /* 0x5B */
	__u32 xattr_prefix_start; /* 0x5C */
	__u64 packed_nid;	/* 0x60 */
	__u8  xattr_filter_reserved; /* 0x68 */
	__u8  reserved[23];	/* 0x69 .. 0x7F */
} __attribute__((packed));

/*
 * EROFS compact inode (32 bytes, 1 NID slot).
 */
struct erofs_inode_compact {
	__u16 i_format;		/* 0x00: version (bit 0) | datalayout (bits 1..3) */
	__u16 i_xattr_icount;	/* 0x02 */
	__u16 i_mode;		/* 0x04 */
	__u16 i_nlink;		/* 0x06 */
	__u32 i_size;		/* 0x08 */
	__u32 i_rsvd;		/* 0x0C */
	__u32 i_u;		/* 0x10: raw_blkaddr or rdev */
	__u32 i_ino;		/* 0x14 */
	__u16 i_uid;		/* 0x18 */
	__u16 i_gid;		/* 0x1A */
	__u32 i_rsvd2;		/* 0x1C */
} __attribute__((packed));

/*
 * EROFS extended inode (64 bytes, 2 NID slots).
 */
struct erofs_inode_extended {
	__u16 i_format;		/* 0x00 */
	__u16 i_xattr_icount;	/* 0x02 */
	__u16 i_mode;		/* 0x04 */
	__u16 i_rsvd;		/* 0x06 */
	__u64 i_size;		/* 0x08 */
	__u32 i_u;		/* 0x10: raw_blkaddr or rdev */
	__u32 i_ino;		/* 0x14 */
	__u32 i_uid;		/* 0x18 */
	__u32 i_gid;		/* 0x1C */
	__u64 i_mtime;		/* 0x20 */
	__u32 i_mtime_nsec;	/* 0x28 */
	__u32 i_nlink;		/* 0x2C */
	__u8  i_rsvd2[16];	/* 0x30 .. 0x3F */
} __attribute__((packed));

/*
 * EROFS directory entry (12 bytes).
 * At the beginning of each directory block, an array of erofs_dirent headers
 * is stored in lexicographical order, followed by the packed filenames.
 * The number of entries in a directory block is (de[0].nameoff / 12).
 */
struct erofs_dirent {
	__u64 nid;		/* 0x00: target inode NID */
	__u16 nameoff;		/* 0x08: start offset of filename in this block */
	__u8  file_type;	/* 0x0A: EROFS_FT_* */
	__u8  reserved;		/* 0x0B */
} __attribute__((packed));

#ifdef __KERNEL__

struct erofs_sb_info {
	struct buffer_head *s_sbh;
	__u32 meta_blkaddr;
	__u32 blocks;
	__u64 inos;
	__u64 build_time;
	__u16 root_nid;
	__u8  blkszbits;
	__u8  uuid[16];
	char  volume_name[17];
};

/*
 * Per-inode metadata stored inside inode->u.ext2_i.i_data[0..3]:
 *   i_data[0] = datalayout (EROFS_INODE_FLAT_PLAIN or EROFS_INODE_FLAT_INLINE)
 *   i_data[1] = raw_blkaddr
 *   i_data[2] = inline_byte_off (byte offset of inline tail data on disk)
 *   i_data[3] = nid
 */
#define EROFS_I_DATALAYOUT(inode)	((inode)->u.ext2_i.i_data[0])
#define EROFS_I_RAW_BLKADDR(inode)	((inode)->u.ext2_i.i_data[1])
#define EROFS_I_INLINE_OFF(inode)	((inode)->u.ext2_i.i_data[2])
#define EROFS_I_NID(inode)		((inode)->u.ext2_i.i_data[3])

extern int init_erofs_fs(void);

#endif /* __KERNEL__ */

#endif /* _LINUX_EROFS_FS_H */
