/*
 * include/linux/ext4_extents.h
 *
 * On-disk extent B+tree structures and constants for SIX's fs/ext4 driver,
 * matching upstream Linux fs/ext4/ext4_extents.h.
 */

#ifndef _LINUX_EXT4_EXTENTS_H
#define _LINUX_EXT4_EXTENTS_H

#include <linux/types.h>

#define EXT4_EXT_MAGIC		0xf30a

/*
 * ext4_extent: leaf node of an extent tree (12 bytes)
 * Maps logical blocks [ee_block .. ee_block + ee_len - 1]
 * to physical blocks [ee_start .. ee_start + ee_len - 1].
 */
struct ext4_extent {
	__u32	ee_block;	/* first logical block extent covers */
	__u16	ee_len;		/* number of blocks covered by extent */
	__u16	ee_start_hi;	/* high 16 bits of physical block */
	__u32	ee_start_lo;	/* low 32 bits of physical block */
};

/*
 * ext4_extent_idx: internal index node of an extent B+tree (12 bytes)
 * Points to child block containing extents/indices starting at ei_block.
 */
struct ext4_extent_idx {
	__u32	ei_block;	/* index covers logical blocks from 'block' */
	__u32	ei_leaf_lo;	/* physical block of next level */
	__u16	ei_leaf_hi;	/* high 16 bits of physical block */
	__u16	ei_unused;
};

/*
 * ext4_extent_header: sits at the start of inode->i_block[15] (60 bytes)
 * and at byte 0 of every external extent tree block (12 bytes).
 */
struct ext4_extent_header {
	__u16	eh_magic;	/* 0xf30a */
	__u16	eh_entries;	/* number of valid entries following header */
	__u16	eh_max;		/* capacity of store in entries (4 in inode) */
	__u16	eh_depth;	/* 0 = leaf extents follow; >0 = index nodes */
	__u32	eh_generation;	/* generation of the tree */
};

#define EXT_FIRST_EXTENT(hdr) \
	((struct ext4_extent *)(((char *)(hdr)) + sizeof(struct ext4_extent_header)))
#define EXT_FIRST_INDEX(hdr) \
	((struct ext4_extent_idx *)(((char *)(hdr)) + sizeof(struct ext4_extent_header)))
#define EXT_LAST_EXTENT(hdr) \
	(EXT_FIRST_EXTENT(hdr) + (hdr)->eh_entries - 1)
#define EXT_LAST_INDEX(hdr) \
	(EXT_FIRST_INDEX(hdr) + (hdr)->eh_entries - 1)

#ifdef __KERNEL__
static inline struct ext4_extent_header *ext_inode_hdr(struct inode *inode)
{
	return (struct ext4_extent_header *)inode->u.ext2_i.i_data;
}

static inline struct ext4_extent_header *ext_block_hdr(struct buffer_head *bh)
{
	return (struct ext4_extent_header *)bh->b_data;
}

static inline int ext4_ext_space_root(void)
{
	return (60 - sizeof(struct ext4_extent_header)) / sizeof(struct ext4_extent);
}

static inline int ext4_ext_space_block(struct super_block *sb)
{
	/* Leave 4 bytes at end of block for ext4_extent_tail checksum */
	return (sb->s_blocksize - sizeof(struct ext4_extent_header) - 4) /
	       sizeof(struct ext4_extent);
}
#endif /* __KERNEL__ */

/*
 * Ioctl structure for /bin/ext4info to inspect live on-disk ext4 metadata
 */
#define EXT4_IOC_GET_INFO	0x6610

struct ext4_info_extent {
	__u32	ee_block;
	__u16	ee_len;
	__u16	ee_start_hi;
	__u32	ee_start_lo;
};

struct ext4_inode_inspect {
	__u32	ino;
	__u32	block_group;
	__u32	inode_table_block;
	__u32	inode_block_offset;
	__u16	inode_size;
	__u16	extra_isize;
	__u32	i_flags;
	__u32	i_size;
	__u32	i_blocks;
	__u16	eh_magic;
	__u16	eh_entries;
	__u16	eh_max;
	__u16	eh_depth;
	__u32	s_feature_compat;
	__u32	s_feature_incompat;
	__u32	s_feature_ro_compat;
	__u32	num_returned_extents;
	struct ext4_info_extent extents[16];
};

#endif /* _LINUX_EXT4_EXTENTS_H */
