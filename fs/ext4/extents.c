/*
 * fs/ext4/extents.c
 *
 * Extent B+Tree implementation for SIX (Linux 2.0.11).
 *
 * Implements the real ext4 extent tree layout (eh_magic = 0xf30a):
 *   - Depth 0: up to 4 struct ext4_extent entries stored directly inside
 *     inode->u.ext2_i.i_data[0..14] (60 bytes).
 *   - Depth >= 1: struct ext4_extent_idx index entries pointing to external
 *     extent blocks on disk (each holding up to (blocksize - 16) / 12 entries).
 *   - Contiguous extent merging (ee_len++) on sequential writes.
 *   - Automatic depth 0 -> depth 1 B+tree promotion (ext4_ext_grow_indepth)
 *     when an inode accumulates more than 4 non-contiguous extents.
 *   - Extent tree truncation (ext4_ext_truncate) freeing all physical block
 *     ranges beyond inode->i_size.
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

/* Unwritten extent flag occupies bit 15 of ee_len in ext4 */
#define EXT_INIT_MAX_LEN	(1UL << 15)

static inline int ext4_ext_get_actual_len(struct ext4_extent *ext)
{
	return (ext->ee_len <= EXT_INIT_MAX_LEN ?
		ext->ee_len : (ext->ee_len - EXT_INIT_MAX_LEN));
}

static inline int ext4_ext_is_unwritten(struct ext4_extent *ext)
{
	return (ext->ee_len > EXT_INIT_MAX_LEN);
}

/*
 * Allocate one physical block for an extent-mapped inode.
 *
 * This is the extent-tree counterpart of ext2_alloc_block() and exists for
 * exactly the same reason: ext4_new_block() does *block preallocation*.  When
 * handed a non-NULL prealloc_count/prealloc_block pair it reserves a run of
 * up to EXT2_DEFAULT_PREALLOC_BLOCKS consecutive blocks in the bitmap, returns
 * the first one, and reports the rest back through those two pointers.  The
 * caller owns that window and must either consume it on subsequent
 * allocations or hand it back via ext4_discard_prealloc().
 *
 * Passing throwaway locals here (as an earlier version of this file did) makes
 * every single-block allocation silently leak the 7 blocks behind it: they
 * stay marked in the block bitmap but are referenced by no inode, which
 * e2fsck reports as "Block bitmap differences", and it also forces every
 * logical block into its own extent because the next allocation has to start
 * a fresh byte of the bitmap.
 *
 * Keeping the window in the inode instead makes sequential writes come out
 * physically contiguous, which is what lets ext4_ext_insert_into_leaf() merge
 * them into a single large extent.
 */
static unsigned long ext4_alloc_block(struct inode *inode, unsigned long goal,
				      int *err)
{
	unsigned long result;
	struct buffer_head *bh;

	wait_on_super(inode->i_sb);

#ifdef EXT2_PREALLOCATE
	if (inode->u.ext2_i.i_prealloc_count &&
	    (goal == inode->u.ext2_i.i_prealloc_block ||
	     goal + 1 == inode->u.ext2_i.i_prealloc_block)) {
		result = inode->u.ext2_i.i_prealloc_block++;
		inode->u.ext2_i.i_prealloc_count--;
		/*
		 * The block is already ours as far as the bitmap is
		 * concerned, so blocking in getblk() while we zero it is
		 * harmless.
		 */
		if (!(bh = getblk(inode->i_sb->s_dev, result,
				  inode->i_sb->s_blocksize))) {
			ext4_error(inode->i_sb, "ext4_alloc_block",
				   "cannot get block %lu", result);
			*err = -EIO;
			return 0;
		}
		memset(bh->b_data, 0, inode->i_sb->s_blocksize);
		mark_buffer_uptodate(bh, 1);
		mark_buffer_dirty(bh, 1);
		brelse(bh);
		*err = 0;
	} else {
		ext4_discard_prealloc(inode);
		if (S_ISREG(inode->i_mode))
			result = ext4_new_block(inode, goal,
						&inode->u.ext2_i.i_prealloc_count,
						&inode->u.ext2_i.i_prealloc_block,
						err);
		else
			result = ext4_new_block(inode, goal, 0, 0, err);
	}
#else
	result = ext4_new_block(inode, goal, 0, 0, err);
#endif

	return result;
}


/*
 * Initialize a freshly allocated inode's 60-byte i_data area as a depth-0
 * ext4 extent tree header (magic = 0xf30a, max = 4).
 */
void ext4_ext_tree_init(struct inode *inode)
{
	struct ext4_extent_header *eh;

	memset(inode->u.ext2_i.i_data, 0, sizeof(inode->u.ext2_i.i_data));
	eh = ext_inode_hdr(inode);
	eh->eh_magic = EXT4_EXT_MAGIC;
	eh->eh_entries = 0;
	eh->eh_max = (unsigned short)ext4_ext_space_root();
	eh->eh_depth = 0;
	eh->eh_generation = 0;
	inode->u.ext2_i.i_flags |= EXT4_EXTENTS_FL;
	inode->i_dirt = 1;
}

/*
 * Search a single leaf extent header (eh_depth == 0) for logical block `iblock`.
 * Returns physical block number (> 0) if mapped, or 0 if unmapped/hole.
 */
static unsigned long ext4_ext_search_leaf(struct ext4_extent_header *eh,
					  unsigned long iblock)
{
	struct ext4_extent *ext = EXT_FIRST_EXTENT(eh);
	int i;
	int entries = eh->eh_entries;

	for (i = 0; i < entries; i++, ext++) {
		unsigned long start = ext->ee_block;
		unsigned long len = ext4_ext_get_actual_len(ext);
		if (iblock >= start && iblock < start + len) {
			if (ext4_ext_is_unwritten(ext))
				return 0;
			return (unsigned long)ext->ee_start_lo + (iblock - start);
		}
	}
	return 0;
}

/*
 * Descend an extent B+tree of arbitrary depth (0, 1, 2, ...) to locate the
 * leaf block buffer_head for `iblock`.
 * If `eh_depth == 0`, sets `*out_bh = NULL` and returns the in-inode header.
 * If `eh_depth > 0`, returns the leaf block's `ext4_extent_header *` and
 * holds a reference in `*out_bh` (caller must brelse(*out_bh)).
 */
static struct ext4_extent_header *ext4_ext_find_leaf(struct inode *inode,
						     unsigned long iblock,
						     struct buffer_head **out_bh)
{
	struct ext4_extent_header *eh = ext_inode_hdr(inode);
	struct buffer_head *bh = NULL;
	int depth;

	*out_bh = NULL;
	if (eh->eh_magic != EXT4_EXT_MAGIC) {
		ext4_error(inode->i_sb, "ext4_ext_find_leaf",
			   "invalid extent magic 0x%x on inode %lu",
			   eh->eh_magic, inode->i_ino);
		return NULL;
	}

	depth = eh->eh_depth;
	while (depth > 0) {
		struct ext4_extent_idx *idx = EXT_FIRST_INDEX(eh);
		struct ext4_extent_idx *target = idx;
		unsigned long next_blk;
		int i;

		if (eh->eh_entries == 0) {
			if (bh) brelse(bh);
			return NULL;
		}
		for (i = 0; i < eh->eh_entries; i++) {
			if (iblock >= idx[i].ei_block)
				target = &idx[i];
			else
				break;
		}
		next_blk = target->ei_leaf_lo;
		if (bh)
			brelse(bh);
		bh = bread(inode->i_dev, next_blk, inode->i_sb->s_blocksize);
		if (!bh) {
			ext4_error(inode->i_sb, "ext4_ext_find_leaf",
				   "unable to read extent index block %lu on inode %lu",
				   next_blk, inode->i_ino);
			return NULL;
		}
		eh = ext_block_hdr(bh);
		if (eh->eh_magic != EXT4_EXT_MAGIC) {
			ext4_error(inode->i_sb, "ext4_ext_find_leaf",
				   "corrupt extent block %lu (magic 0x%x) on inode %lu",
				   next_blk, eh->eh_magic, inode->i_ino);
			brelse(bh);
			return NULL;
		}
		depth = eh->eh_depth;
	}

	*out_bh = bh;
	return eh;
}

/*
 * Insert `(iblock, newblock)` into a leaf extent array `eh` that has
 * `eh->eh_entries < eh->eh_max`, or merge with an adjacent extent.
 * Returns 1 if inserted/merged, 0 if the leaf is full.
 */
static int ext4_ext_insert_into_leaf(struct ext4_extent_header *eh,
				     unsigned long iblock,
				     unsigned long newblock)
{
	struct ext4_extent *ext = EXT_FIRST_EXTENT(eh);
	int entries = eh->eh_entries;
	int i, insert_pos = entries;

	/* 1. Try to merge with an existing contiguous extent */
	for (i = 0; i < entries; i++) {
		unsigned long e_blk = ext[i].ee_block;
		unsigned long e_len = ext4_ext_get_actual_len(&ext[i]);
		unsigned long e_phys = ext[i].ee_start_lo;

		if (!ext4_ext_is_unwritten(&ext[i]) &&
		    e_blk + e_len == iblock &&
		    e_phys + e_len == newblock &&
		    e_len < 32767) {
			ext[i].ee_len = (unsigned short)(e_len + 1);
			return 1;
		}
		if (iblock < e_blk && insert_pos == entries)
			insert_pos = i;
	}

	/* 2. Otherwise insert a new 1-block extent at `insert_pos` if space permits */
	if (entries >= eh->eh_max)
		return 0;

	for (i = entries; i > insert_pos; i--)
		ext[i] = ext[i - 1];

	ext[insert_pos].ee_block = (__u32)iblock;
	ext[insert_pos].ee_len = 1;
	ext[insert_pos].ee_start_hi = 0;
	ext[insert_pos].ee_start_lo = (__u32)newblock;
	eh->eh_entries++;
	return 1;
}

/*
 * Promote a full depth-0 in-inode extent tree (eh_entries == 4) to depth 1
 * by allocating one external leaf block, copying the 4 existing extents into
 * it, and setting the root header to 1 index entry pointing to that block.
 */
static int ext4_ext_grow_indepth(struct inode *inode, int *err)
{
	struct ext4_extent_header *root_eh = ext_inode_hdr(inode);
	struct ext4_extent_header *leaf_eh;
	struct ext4_extent_idx *root_idx;
	struct buffer_head *bh;
	unsigned long goal = inode->u.ext2_i.i_block_group *
			     EXT2_BLOCKS_PER_GROUP(inode->i_sb) +
			     inode->i_sb->u.ext2_sb.s_es->s_first_data_block;
	int new_leaf_blk;

	/*
	 * This is a metadata (index) block, not file data, so ask for it with
	 * no preallocation window -- passing NULL keeps ext4_new_block() from
	 * reserving a run we would then have to track and release.
	 */
	new_leaf_blk = ext4_new_block(inode, goal, 0, 0, err);
	if (!new_leaf_blk)
		return 0;

	bh = getblk(inode->i_sb->s_dev, new_leaf_blk, inode->i_sb->s_blocksize);
	if (!bh) {
		*err = -EIO;
		return 0;
	}
	memset(bh->b_data, 0, inode->i_sb->s_blocksize);
	leaf_eh = ext_block_hdr(bh);
	leaf_eh->eh_magic = EXT4_EXT_MAGIC;
	leaf_eh->eh_entries = root_eh->eh_entries;
	leaf_eh->eh_max = (unsigned short)ext4_ext_space_block(inode->i_sb);
	leaf_eh->eh_depth = 0;
	leaf_eh->eh_generation = root_eh->eh_generation;

	memcpy(EXT_FIRST_EXTENT(leaf_eh), EXT_FIRST_EXTENT(root_eh),
	       root_eh->eh_entries * sizeof(struct ext4_extent));

	mark_buffer_uptodate(bh, 1);
	mark_buffer_dirty(bh, 1);
	brelse(bh);

	/* Rewrite root in-inode header as depth=1 with 1 index entry */
	root_idx = EXT_FIRST_INDEX(root_eh);
	root_idx->ei_block = EXT_FIRST_EXTENT(leaf_eh)->ee_block;
	root_idx->ei_leaf_lo = (__u32)new_leaf_blk;
	root_idx->ei_leaf_hi = 0;
	root_idx->ei_unused = 0;
	memset(root_idx + 1, 0, 3 * sizeof(struct ext4_extent));

	root_eh->eh_entries = 1;
	root_eh->eh_depth = 1;
	inode->i_blocks += inode->i_sb->s_blocksize / 512;
	inode->i_dirt = 1;
	return 1;
}

/*
 * Map logical block `iblock` of an extent-mapped inode (`EXT4_EXTENTS_FL`)
 * to its physical disk block number. If `create != 0` and `iblock` is not
 * yet mapped, allocates a physical block and inserts it into the extent tree.
 */
int ext4_ext_get_block(struct inode *inode, long iblock, int create, int *err)
{
	struct buffer_head *leaf_bh = NULL;
	struct ext4_extent_header *eh;
	unsigned long phys;
	unsigned long goal;
	int newblock;

	*err = 0;
	if (iblock < 0) {
		ext4_warning(inode->i_sb, "ext4_ext_get_block", "block < 0");
		*err = -EIO;
		return 0;
	}

	eh = ext4_ext_find_leaf(inode, (unsigned long)iblock, &leaf_bh);
	if (!eh) {
		*err = -EIO;
		return 0;
	}

	phys = ext4_ext_search_leaf(eh, (unsigned long)iblock);
	if (phys != 0) {
		if (leaf_bh)
			brelse(leaf_bh);
		return (int)phys;
	}

	if (!create) {
		if (leaf_bh)
			brelse(leaf_bh);
		return 0;
	}

	/* Determine allocation goal from last extent in leaf if available */
	if (eh->eh_entries > 0) {
		struct ext4_extent *last = EXT_LAST_EXTENT(eh);
		goal = last->ee_start_lo + ext4_ext_get_actual_len(last);
	} else {
		goal = (inode->u.ext2_i.i_block_group *
			EXT2_BLOCKS_PER_GROUP(inode->i_sb)) +
		       inode->i_sb->u.ext2_sb.s_es->s_first_data_block;
	}

	newblock = (int)ext4_alloc_block(inode, goal, err);
	if (!newblock) {
		if (leaf_bh)
			brelse(leaf_bh);
		return 0;
	}

	/* Try inserting into the current leaf */
	if (ext4_ext_insert_into_leaf(eh, (unsigned long)iblock, (unsigned long)newblock)) {
		if (leaf_bh) {
			mark_buffer_dirty(leaf_bh, 1);
			brelse(leaf_bh);
		}
		inode->i_blocks += inode->i_sb->s_blocksize / 512;
		inode->i_dirt = 1;
		return newblock;
	}

	/* If in-inode leaf (depth 0) is full, promote to depth 1 and insert */
	if (!leaf_bh && eh->eh_depth == 0) {
		if (ext4_ext_grow_indepth(inode, err)) {
			eh = ext4_ext_find_leaf(inode, (unsigned long)iblock, &leaf_bh);
			if (eh && ext4_ext_insert_into_leaf(eh, (unsigned long)iblock,
							    (unsigned long)newblock)) {
				if (leaf_bh) {
					mark_buffer_dirty(leaf_bh, 1);
					brelse(leaf_bh);
				}
				inode->i_blocks += inode->i_sb->s_blocksize / 512;
				inode->i_dirt = 1;
				return newblock;
			}
		}
	}

	if (leaf_bh)
		brelse(leaf_bh);
	ext4_free_blocks(inode, (unsigned long)newblock, 1);
	*err = -ENOSPC;
	return 0;
}

struct buffer_head *ext4_ext_getblk(struct inode *inode, long block,
				    int create, int *err)
{
	struct buffer_head *result;
	int phys;
	int was_unmapped;

	*err = 0;
	phys = ext4_ext_get_block(inode, block, 0, err);
	if (phys != 0)
		return getblk(inode->i_sb->s_dev, phys, inode->i_sb->s_blocksize);

	if (!create)
		return NULL;

	was_unmapped = 1;
	phys = ext4_ext_get_block(inode, block, 1, err);
	if (!phys)
		return NULL;

	result = getblk(inode->i_sb->s_dev, phys, inode->i_sb->s_blocksize);
	if (result && was_unmapped) {
		memset(result->b_data, 0, inode->i_sb->s_blocksize);
		mark_buffer_uptodate(result, 1);
		mark_buffer_dirty(result, 1);
	}
	return result;
}

/*
 * Free all blocks in a leaf extent block beyond `first_free_iblock`.
 * Returns number of remaining valid extents in the leaf.
 */
static int ext4_ext_truncate_leaf(struct inode *inode,
				  struct ext4_extent_header *eh,
				  unsigned long first_free_iblock)
{
	struct ext4_extent *ext = EXT_FIRST_EXTENT(eh);
	int i;
	int new_entries = 0;
	int blocks_per_fs_block = inode->i_sb->s_blocksize / 512;

	for (i = 0; i < eh->eh_entries; i++) {
		unsigned long start = ext[i].ee_block;
		unsigned long len = ext4_ext_get_actual_len(&ext[i]);
		unsigned long phys = ext[i].ee_start_lo;

		if (start >= first_free_iblock) {
			/* Entire extent is past new EOF */
			ext4_free_blocks(inode, phys, len);
			if (inode->i_blocks >= len * blocks_per_fs_block)
				inode->i_blocks -= len * blocks_per_fs_block;
			else
				inode->i_blocks = 0;
		} else if (start + len > first_free_iblock) {
			/* Partial extent truncation */
			unsigned long keep = first_free_iblock - start;
			unsigned long drop = len - keep;
			ext4_free_blocks(inode, phys + keep, drop);
			ext[i].ee_len = (unsigned short)keep;
			if (inode->i_blocks >= drop * blocks_per_fs_block)
				inode->i_blocks -= drop * blocks_per_fs_block;
			else
				inode->i_blocks = 0;
			new_entries++;
		} else {
			new_entries++;
		}
	}
	eh->eh_entries = (unsigned short)new_entries;
	return new_entries;
}

/*
 * Recursively truncate an extent subtree rooted at `eh` for blocks >= `first_free_iblock`.
 */
static void ext4_ext_truncate_subtree(struct inode *inode,
				      struct ext4_extent_header *eh,
				      unsigned long first_free_iblock)
{
	int i;
	int blocks_per_fs_block = inode->i_sb->s_blocksize / 512;

	if (eh->eh_magic != EXT4_EXT_MAGIC)
		return;

	if (eh->eh_depth == 0) {
		ext4_ext_truncate_leaf(inode, eh, first_free_iblock);
		return;
	}

	{
		struct ext4_extent_idx *idx = EXT_FIRST_INDEX(eh);
		int kept_idx = 0;
		for (i = 0; i < eh->eh_entries; i++) {
			unsigned long child_blk = idx[i].ei_leaf_lo;
			struct buffer_head *bh = bread(inode->i_dev, child_blk,
						       inode->i_sb->s_blocksize);
			if (bh) {
				struct ext4_extent_header *child_eh = ext_block_hdr(bh);
				ext4_ext_truncate_subtree(inode, child_eh, first_free_iblock);
				if (child_eh->eh_entries == 0) {
					brelse(bh);
					ext4_free_blocks(inode, child_blk, 1);
					if (inode->i_blocks >= (unsigned long)blocks_per_fs_block)
						inode->i_blocks -= blocks_per_fs_block;
				} else {
					mark_buffer_dirty(bh, 1);
					brelse(bh);
					kept_idx++;
				}
			}
		}
		eh->eh_entries = (unsigned short)kept_idx;
		if (kept_idx == 0)
			eh->eh_depth = 0;
	}
}

void ext4_ext_truncate(struct inode *inode)
{
	unsigned long blocksize = inode->i_sb->s_blocksize;
	unsigned long first_free = (inode->i_size + blocksize - 1) / blocksize;
	struct ext4_extent_header *eh = ext_inode_hdr(inode);

	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) || S_ISLNK(inode->i_mode)))
		return;

	ext4_discard_prealloc(inode);
	ext4_ext_truncate_subtree(inode, eh, first_free);
	if (eh->eh_entries == 0 && eh->eh_depth == 0)
		eh->eh_max = (unsigned short)ext4_ext_space_root();
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_dirt = 1;
}

/*
 * Populate `struct ext4_inode_inspect` for `/bin/ext4info`
 */
int ext4_ext_inspect(struct inode *inode, struct ext4_inode_inspect *info)
{
	struct super_block *sb = inode->i_sb;
	struct ext2_super_block *es = sb->u.ext2_sb.s_es;
	unsigned long block_group;
	unsigned long group_desc;
	unsigned long desc;
	unsigned long offset;
	struct buffer_head *bh;
	struct ext2_group_desc *gdp;
	struct ext4_extent_header *eh = ext_inode_hdr(inode);

	memset(info, 0, sizeof(*info));
	info->ino = (__u32)inode->i_ino;
	block_group = (inode->i_ino - 1) / EXT2_INODES_PER_GROUP(sb);
	info->block_group = (__u32)block_group;

	group_desc = block_group >> EXT2_DESC_PER_BLOCK_BITS(sb);
	desc = block_group & (EXT2_DESC_PER_BLOCK(sb) - 1);
	bh = sb->u.ext2_sb.s_group_desc[group_desc];
	if (bh) {
		gdp = (struct ext2_group_desc *)bh->b_data;
		offset = ((inode->i_ino - 1) % EXT2_INODES_PER_GROUP(sb)) *
			 EXT2_INODE_SIZE(sb);
		info->inode_table_block = (__u32)(gdp[desc].bg_inode_table +
					  (offset >> EXT2_BLOCK_SIZE_BITS(sb)));
		info->inode_block_offset = (__u32)(offset & (EXT2_BLOCK_SIZE(sb) - 1));
	}
	info->inode_size = (__u16)EXT2_INODE_SIZE(sb);
	info->extra_isize = (info->inode_size > 128) ? 32 : 0;
	info->i_flags = inode->u.ext2_i.i_flags;
	info->i_size = (__u32)inode->i_size;
	info->i_blocks = (__u32)inode->i_blocks;
	info->s_feature_compat = es->s_feature_compat;
	info->s_feature_incompat = es->s_feature_incompat;
	info->s_feature_ro_compat = es->s_feature_ro_compat;

	if (inode->u.ext2_i.i_flags & EXT4_EXTENTS_FL) {
		struct buffer_head *leaf_bh = NULL;
		struct ext4_extent_header *leaf_eh;
		info->eh_magic = eh->eh_magic;
		info->eh_entries = eh->eh_entries;
		info->eh_max = eh->eh_max;
		info->eh_depth = eh->eh_depth;

		leaf_eh = ext4_ext_find_leaf(inode, 0, &leaf_bh);
		if (leaf_eh) {
			struct ext4_extent *ext = EXT_FIRST_EXTENT(leaf_eh);
			int n = leaf_eh->eh_entries;
			int i;
			if (n > 16) n = 16;
			info->num_returned_extents = (__u32)n;
			for (i = 0; i < n; i++) {
				info->extents[i].ee_block = ext[i].ee_block;
				info->extents[i].ee_len = ext[i].ee_len;
				info->extents[i].ee_start_hi = ext[i].ee_start_hi;
				info->extents[i].ee_start_lo = ext[i].ee_start_lo;
			}
			if (leaf_bh)
				brelse(leaf_bh);
		}
	}
	return 0;
}
