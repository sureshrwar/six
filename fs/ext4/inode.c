/*
 * fs/ext4/inode.c
 *
 * Inode block mapping (dispatching to fs/ext4/extents.c for EXT4_EXTENTS_FL
 * inodes and indirect blocks for legacy inodes) and 256-byte dynamic inode
 * reading/writing for SIX's fs/ext4 driver.
 */

#include <asm/segment.h>
#include <asm/system.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ext4_fs.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/locks.h>
#include <linux/mm.h>

static int ext4_update_inode(struct inode *inode, int do_sync);

void ext4_put_inode(struct inode *inode)
{
	ext4_discard_prealloc(inode);
	if (inode->i_nlink || inode->i_ino == EXT2_ACL_IDX_INO ||
	    inode->i_ino == EXT2_ACL_DATA_INO)
		return;
	inode->u.ext2_i.i_dtime = CURRENT_TIME;
	inode->i_dirt = 1;
	ext4_update_inode(inode, IS_SYNC(inode));
	inode->i_size = 0;
	if (inode->i_blocks)
		ext4_truncate(inode);
	ext4_free_inode(inode);
}

#define inode_bmap(inode, nr) ((inode)->u.ext2_i.i_data[(nr)])

static inline int block_bmap(struct buffer_head *bh, int nr)
{
	int tmp;

	if (!bh)
		return 0;
	tmp = ((u32 *)bh->b_data)[nr];
	brelse(bh);
	return tmp;
}

void ext4_discard_prealloc(struct inode *inode)
{
#ifdef EXT2_PREALLOCATE
	unsigned short total;

	if (inode->u.ext2_i.i_prealloc_count) {
		total = inode->u.ext2_i.i_prealloc_count;
		inode->u.ext2_i.i_prealloc_count = 0;
		ext4_free_blocks(inode, inode->u.ext2_i.i_prealloc_block, total);
	}
#endif
}

int ext4_bmap(struct inode *inode, int block)
{
	int i;
	int addr_per_block = EXT2_ADDR_PER_BLOCK(inode->i_sb);
	int addr_per_block_bits = EXT2_ADDR_PER_BLOCK_BITS(inode->i_sb);

	if (inode->u.ext2_i.i_flags & EXT4_EXTENTS_FL) {
		int err = 0;
		return ext4_ext_get_block(inode, block, 0, &err);
	}

	if (block < 0) {
		ext4_warning(inode->i_sb, "ext4_bmap", "block < 0");
		return 0;
	}
	if (block >= EXT2_NDIR_BLOCKS + addr_per_block +
		(1 << (addr_per_block_bits * 2)) +
		((1 << (addr_per_block_bits * 2)) << addr_per_block_bits)) {
		ext4_warning(inode->i_sb, "ext4_bmap", "block > big");
		return 0;
	}
	if (block < EXT2_NDIR_BLOCKS)
		return inode_bmap(inode, block);
	block -= EXT2_NDIR_BLOCKS;
	if (block < addr_per_block) {
		i = inode_bmap(inode, EXT2_IND_BLOCK);
		if (!i)
			return 0;
		return block_bmap(bread(inode->i_dev, i,
					inode->i_sb->s_blocksize), block);
	}
	block -= addr_per_block;
	if (block < (1 << (addr_per_block_bits * 2))) {
		i = inode_bmap(inode, EXT2_DIND_BLOCK);
		if (!i)
			return 0;
		i = block_bmap(bread(inode->i_dev, i,
				     inode->i_sb->s_blocksize),
			       block >> addr_per_block_bits);
		if (!i)
			return 0;
		return block_bmap(bread(inode->i_dev, i,
					inode->i_sb->s_blocksize),
				  block & (addr_per_block - 1));
	}
	block -= (1 << (addr_per_block_bits * 2));
	i = inode_bmap(inode, EXT2_TIND_BLOCK);
	if (!i)
		return 0;
	i = block_bmap(bread(inode->i_dev, i, inode->i_sb->s_blocksize),
		       block >> (addr_per_block_bits * 2));
	if (!i)
		return 0;
	i = block_bmap(bread(inode->i_dev, i, inode->i_sb->s_blocksize),
		       (block >> addr_per_block_bits) & (addr_per_block - 1));
	if (!i)
		return 0;
	return block_bmap(bread(inode->i_dev, i, inode->i_sb->s_blocksize),
			  block & (addr_per_block - 1));
}

struct buffer_head *ext4_getblk(struct inode *inode, long block,
				int create, int *err)
{
	if (inode->u.ext2_i.i_flags & EXT4_EXTENTS_FL)
		return ext4_ext_getblk(inode, block, create, err);

	/* Fallback for non-extent inodes: promote to extents if empty */
	if (create && inode->i_blocks == 0 &&
	    (S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode))) {
		ext4_ext_tree_init(inode);
		return ext4_ext_getblk(inode, block, create, err);
	}

	{
		int phys = ext4_bmap(inode, (int)block);
		if (phys)
			return getblk(inode->i_sb->s_dev, phys, inode->i_sb->s_blocksize);
		*err = -EIO;
		return NULL;
	}
}

struct buffer_head *ext4_bread(struct inode *inode, int block,
			       int create, int *err)
{
	struct buffer_head *bh;

	bh = ext4_getblk(inode, block, create, err);
	if (!bh || buffer_uptodate(bh))
		return bh;
	ll_rw_block(READ, 1, &bh);
	wait_on_buffer(bh);
	if (buffer_uptodate(bh))
		return bh;
	brelse(bh);
	*err = -EIO;
	return NULL;
}

void ext4_read_inode(struct inode *inode)
{
	struct buffer_head *bh;
	struct ext2_inode *raw_inode;
	unsigned long block_group;
	unsigned long group_desc;
	unsigned long desc;
	unsigned long block;
	unsigned long offset;
	struct ext2_group_desc *gdp;

	if ((inode->i_ino != EXT2_ROOT_INO && inode->i_ino != EXT2_ACL_IDX_INO &&
	     inode->i_ino != EXT2_ACL_DATA_INO &&
	     inode->i_ino < EXT2_FIRST_INO(inode->i_sb)) ||
	    inode->i_ino > inode->i_sb->u.ext2_sb.s_es->s_inodes_count) {
		ext4_error(inode->i_sb, "ext4_read_inode",
			   "bad inode number: %lu", inode->i_ino);
		return;
	}
	block_group = (inode->i_ino - 1) / EXT2_INODES_PER_GROUP(inode->i_sb);
	if (block_group >= inode->i_sb->u.ext2_sb.s_groups_count)
		ext4_panic(inode->i_sb, "ext4_read_inode",
			   "group >= groups count");
	group_desc = block_group >> EXT2_DESC_PER_BLOCK_BITS(inode->i_sb);
	desc = block_group & (EXT2_DESC_PER_BLOCK(inode->i_sb) - 1);
	bh = inode->i_sb->u.ext2_sb.s_group_desc[group_desc];
	if (!bh)
		ext4_panic(inode->i_sb, "ext4_read_inode",
			   "Descriptor not loaded");
	gdp = (struct ext2_group_desc *)bh->b_data;

	offset = ((inode->i_ino - 1) % EXT2_INODES_PER_GROUP(inode->i_sb)) *
		 EXT2_INODE_SIZE(inode->i_sb);
	block = gdp[desc].bg_inode_table +
		(offset >> EXT2_BLOCK_SIZE_BITS(inode->i_sb));
	if (!(bh = bread(inode->i_dev, block, inode->i_sb->s_blocksize)))
		ext4_panic(inode->i_sb, "ext4_read_inode",
			   "unable to read i-node block - inode=%lu, block=%lu",
			   inode->i_ino, block);
	offset &= (EXT2_BLOCK_SIZE(inode->i_sb) - 1);
	raw_inode = (struct ext2_inode *)(bh->b_data + offset);

	inode->i_mode = raw_inode->i_mode;
	inode->i_uid = raw_inode->i_uid;
	inode->i_gid = raw_inode->i_gid;
	inode->i_nlink = raw_inode->i_links_count;
	inode->i_size = raw_inode->i_size;
	inode->i_atime = raw_inode->i_atime;
	inode->i_ctime = raw_inode->i_ctime;
	inode->i_mtime = raw_inode->i_mtime;
	inode->u.ext2_i.i_dtime = raw_inode->i_dtime;
	inode->i_blksize = PAGE_SIZE;
	inode->i_blocks = raw_inode->i_blocks;
	inode->i_version = ++event;
	inode->u.ext2_i.i_new_inode = 0;
	inode->u.ext2_i.i_flags = raw_inode->i_flags;
	inode->u.ext2_i.i_faddr = raw_inode->i_faddr;
	inode->u.ext2_i.i_frag_no = raw_inode->i_frag;
	inode->u.ext2_i.i_frag_size = raw_inode->i_fsize;
	inode->u.ext2_i.i_osync = 0;
	inode->u.ext2_i.i_file_acl = raw_inode->i_file_acl;
	inode->u.ext2_i.i_dir_acl = raw_inode->i_dir_acl;
	inode->u.ext2_i.i_version = raw_inode->i_version;
	inode->u.ext2_i.i_block_group = block_group;
	inode->u.ext2_i.i_next_alloc_block = 0;
	inode->u.ext2_i.i_next_alloc_goal = 0;
	if (inode->u.ext2_i.i_prealloc_count)
		ext4_error(inode->i_sb, "ext4_read_inode",
			   "New inode has non-zero prealloc count!");
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		inode->i_rdev = to_kdev_t(raw_inode->i_block[0]);
	else
		for (block = 0; block < EXT2_N_BLOCKS; block++)
			inode->u.ext2_i.i_data[block] = raw_inode->i_block[block];
	brelse(bh);

	inode->i_op = NULL;
	if (inode->i_ino == EXT2_ACL_IDX_INO ||
	    inode->i_ino == EXT2_ACL_DATA_INO)
		/* Nothing to do */ ;
	else if (S_ISREG(inode->i_mode))
		inode->i_op = &ext4_file_inode_operations;
	else if (S_ISDIR(inode->i_mode))
		inode->i_op = &ext4_dir_inode_operations;
	else if (S_ISLNK(inode->i_mode))
		inode->i_op = &ext4_symlink_inode_operations;
	else if (S_ISCHR(inode->i_mode))
		inode->i_op = &chrdev_inode_operations;
	else if (S_ISBLK(inode->i_mode))
		inode->i_op = &blkdev_inode_operations;
	else if (S_ISFIFO(inode->i_mode))
		init_fifo(inode);
	if (inode->u.ext2_i.i_flags & EXT2_SYNC_FL)
		inode->i_flags |= MS_SYNCHRONOUS;
	if (inode->u.ext2_i.i_flags & EXT2_APPEND_FL)
		inode->i_flags |= S_APPEND;
	if (inode->u.ext2_i.i_flags & EXT2_IMMUTABLE_FL)
		inode->i_flags |= S_IMMUTABLE;
}

static int ext4_update_inode(struct inode *inode, int do_sync)
{
	struct buffer_head *bh;
	struct ext2_inode *raw_inode;
	unsigned long block_group;
	unsigned long group_desc;
	unsigned long desc;
	unsigned long block;
	unsigned long offset;
	int err = 0;
	struct ext2_group_desc *gdp;

	if ((inode->i_ino != EXT2_ROOT_INO &&
	     inode->i_ino < EXT2_FIRST_INO(inode->i_sb)) ||
	    inode->i_ino > inode->i_sb->u.ext2_sb.s_es->s_inodes_count) {
		ext4_error(inode->i_sb, "ext4_write_inode",
			   "bad inode number: %lu", inode->i_ino);
		return 0;
	}
	block_group = (inode->i_ino - 1) / EXT2_INODES_PER_GROUP(inode->i_sb);
	if (block_group >= inode->i_sb->u.ext2_sb.s_groups_count)
		ext4_panic(inode->i_sb, "ext4_write_inode",
			   "group >= groups count");
	group_desc = block_group >> EXT2_DESC_PER_BLOCK_BITS(inode->i_sb);
	desc = block_group & (EXT2_DESC_PER_BLOCK(inode->i_sb) - 1);
	bh = inode->i_sb->u.ext2_sb.s_group_desc[group_desc];
	if (!bh)
		ext4_panic(inode->i_sb, "ext4_write_inode",
			   "Descriptor not loaded");
	gdp = (struct ext2_group_desc *)bh->b_data;

	offset = ((inode->i_ino - 1) % EXT2_INODES_PER_GROUP(inode->i_sb)) *
		 EXT2_INODE_SIZE(inode->i_sb);
	block = gdp[desc].bg_inode_table +
		(offset >> EXT2_BLOCK_SIZE_BITS(inode->i_sb));
	if (!(bh = bread(inode->i_dev, block, inode->i_sb->s_blocksize)))
		ext4_panic(inode->i_sb, "ext4_write_inode",
			   "unable to read i-node block - inode=%lu, block=%lu",
			   inode->i_ino, block);
	offset &= EXT2_BLOCK_SIZE(inode->i_sb) - 1;
	raw_inode = (struct ext2_inode *)(bh->b_data + offset);

	raw_inode->i_mode = inode->i_mode;
	raw_inode->i_uid = inode->i_uid;
	raw_inode->i_gid = inode->i_gid;
	raw_inode->i_links_count = inode->i_nlink;
	raw_inode->i_size = inode->i_size;
	raw_inode->i_atime = inode->i_atime;
	raw_inode->i_ctime = inode->i_ctime;
	raw_inode->i_mtime = inode->i_mtime;
	raw_inode->i_blocks = inode->i_blocks;
	raw_inode->i_dtime = inode->u.ext2_i.i_dtime;
	raw_inode->i_flags = inode->u.ext2_i.i_flags;
	raw_inode->i_faddr = inode->u.ext2_i.i_faddr;
	raw_inode->i_frag = inode->u.ext2_i.i_frag_no;
	raw_inode->i_fsize = inode->u.ext2_i.i_frag_size;
	raw_inode->i_file_acl = inode->u.ext2_i.i_file_acl;
	raw_inode->i_dir_acl = S_ISREG(inode->i_mode) ? 0 : inode->u.ext2_i.i_dir_acl;
	raw_inode->i_version = inode->u.ext2_i.i_version;
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		raw_inode->i_block[0] = kdev_t_to_nr(inode->i_rdev);
	else
		for (block = 0; block < EXT2_N_BLOCKS; block++)
			raw_inode->i_block[block] = inode->u.ext2_i.i_data[block];

	/* Initialize extra_isize (32 bytes) on 256-byte dynamic ext4 inodes */
	if (EXT2_INODE_SIZE(inode->i_sb) >= 160) {
		__u16 *extra_isize = (__u16 *)(((char *)raw_inode) + 128);
		if (*extra_isize == 0)
			*extra_isize = 32;
	}

	mark_buffer_dirty(bh, 1);
	inode->i_dirt = 0;
	if (do_sync) {
		ll_rw_block(WRITE, 1, &bh);
		wait_on_buffer(bh);
		if (buffer_req(bh) && !buffer_uptodate(bh)) {
			printk("IO error syncing ext4 inode [%s:%08lx]\n",
			       kdevname(inode->i_dev), inode->i_ino);
			err = -1;
		}
	}
	brelse(bh);
	return err;
}

void ext4_write_inode(struct inode *inode)
{
	ext4_update_inode(inode, 0);
}

int ext4_sync_inode(struct inode *inode)
{
	return ext4_update_inode(inode, 1);
}
