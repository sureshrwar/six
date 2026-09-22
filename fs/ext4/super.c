/*
 * fs/ext4/super.c
 *
 * Superblock, group descriptor (including flex_bg), and VFS registration
 * for SIX's fs/ext4 filesystem driver.
 */

#include <linux/module.h>
#include <stdarg.h>

#include <asm/bitops.h>
#include <asm/segment.h>
#include <asm/system.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ext4_fs.h>
#include <linux/malloc.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/locks.h>

static char error_buf[1024];

void ext4_error(struct super_block *sb, const char *function,
		const char *fmt, ...)
{
	va_list args;

	if (!(sb->s_flags & MS_RDONLY)) {
		sb->u.ext2_sb.s_mount_state |= EXT2_ERROR_FS;
		sb->u.ext2_sb.s_es->s_state |= EXT2_ERROR_FS;
		mark_buffer_dirty(sb->u.ext2_sb.s_sbh, 1);
		sb->s_dirt = 1;
	}
	va_start(args, fmt);
	vsprintf(error_buf, fmt, args);
	va_end(args);
	if (test_opt(sb, ERRORS_PANIC) ||
	    (sb->u.ext2_sb.s_es->s_errors == EXT2_ERRORS_PANIC &&
	     !test_opt(sb, ERRORS_CONT) && !test_opt(sb, ERRORS_RO)))
		panic("EXT4-fs panic (device %s): %s: %s\n",
		      kdevname(sb->s_dev), function, error_buf);
	printk(KERN_CRIT "EXT4-fs error (device %s): %s: %s\n",
	       kdevname(sb->s_dev), function, error_buf);
	if (test_opt(sb, ERRORS_RO) ||
	    (sb->u.ext2_sb.s_es->s_errors == EXT2_ERRORS_RO &&
	     !test_opt(sb, ERRORS_CONT) && !test_opt(sb, ERRORS_PANIC))) {
		printk("EXT4-fs: Remounting filesystem read-only\n");
		sb->s_flags |= MS_RDONLY;
	}
}

NORET_TYPE void ext4_panic(struct super_block *sb, const char *function,
			   const char *fmt, ...)
{
	va_list args;

	if (!(sb->s_flags & MS_RDONLY)) {
		sb->u.ext2_sb.s_mount_state |= EXT2_ERROR_FS;
		sb->u.ext2_sb.s_es->s_state |= EXT2_ERROR_FS;
		mark_buffer_dirty(sb->u.ext2_sb.s_sbh, 1);
		sb->s_dirt = 1;
	}
	va_start(args, fmt);
	vsprintf(error_buf, fmt, args);
	va_end(args);
	if (sb->s_lock)
		sb->s_lock = 0;
	sb->s_flags |= MS_RDONLY;
	panic("EXT4-fs panic (device %s): %s: %s\n",
	      kdevname(sb->s_dev), function, error_buf);
}

void ext4_warning(struct super_block *sb, const char *function,
		  const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsprintf(error_buf, fmt, args);
	va_end(args);
	printk(KERN_WARNING "EXT4-fs warning (device %s): %s: %s\n",
	       kdevname(sb->s_dev), function, error_buf);
}

void ext4_put_super(struct super_block *sb)
{
	int db_count;
	int i;

	lock_super(sb);
	if (!(sb->s_flags & MS_RDONLY)) {
		sb->u.ext2_sb.s_es->s_state = sb->u.ext2_sb.s_mount_state;
		mark_buffer_dirty(sb->u.ext2_sb.s_sbh, 1);
	}
	sb->s_dev = 0;
	db_count = sb->u.ext2_sb.s_db_per_group;
	for (i = 0; i < db_count; i++)
		if (sb->u.ext2_sb.s_group_desc[i])
			brelse(sb->u.ext2_sb.s_group_desc[i]);
	kfree_s(sb->u.ext2_sb.s_group_desc,
		db_count * sizeof(struct buffer_head *));
	for (i = 0; i < EXT2_MAX_GROUP_LOADED; i++)
		if (sb->u.ext2_sb.s_inode_bitmap[i])
			brelse(sb->u.ext2_sb.s_inode_bitmap[i]);
	for (i = 0; i < EXT2_MAX_GROUP_LOADED; i++)
		if (sb->u.ext2_sb.s_block_bitmap[i])
			brelse(sb->u.ext2_sb.s_block_bitmap[i]);
	brelse(sb->u.ext2_sb.s_sbh);
	unlock_super(sb);
	MOD_DEC_USE_COUNT;
}

static struct super_operations ext4_sops = {
	ext4_read_inode,
	NULL,
	ext4_write_inode,
	ext4_put_inode,
	ext4_put_super,
	ext4_write_super,
	ext4_statfs,
	ext4_remount
};

/*
 * Check group descriptors. Supports both traditional per-group metadata
 * placement and ext4's FLEX_BG feature (where bitmaps and inode tables of
 * multiple block groups are packed together into the first group of a flex_bg).
 */
static int ext4_check_descriptors(struct super_block *sb)
{
	int i;
	int desc_block = 0;
	unsigned long first_block = sb->u.ext2_sb.s_es->s_first_data_block;
	unsigned long last_block = sb->u.ext2_sb.s_es->s_blocks_count;
	unsigned long group_first = first_block;
	int flex_bg = (sb->u.ext2_sb.s_es->s_feature_incompat &
		       EXT4_FEATURE_INCOMPAT_FLEX_BG) != 0;
	struct ext2_group_desc *gdp = NULL;

	for (i = 0; i < sb->u.ext2_sb.s_groups_count; i++) {
		unsigned long min_blk = flex_bg ? first_block : group_first;
		unsigned long max_blk = flex_bg ? last_block :
					(group_first + EXT2_BLOCKS_PER_GROUP(sb));
		if (max_blk > last_block)
			max_blk = last_block;

		if ((i % EXT2_DESC_PER_BLOCK(sb)) == 0)
			gdp = (struct ext2_group_desc *)
			      sb->u.ext2_sb.s_group_desc[desc_block++]->b_data;

		if (gdp->bg_block_bitmap < min_blk || gdp->bg_block_bitmap >= max_blk) {
			ext4_error(sb, "ext4_check_descriptors",
				   "Block bitmap for group %d out of range (block %lu)!",
				   i, (unsigned long)gdp->bg_block_bitmap);
			return 0;
		}
		if (gdp->bg_inode_bitmap < min_blk || gdp->bg_inode_bitmap >= max_blk) {
			ext4_error(sb, "ext4_check_descriptors",
				   "Inode bitmap for group %d out of range (block %lu)!",
				   i, (unsigned long)gdp->bg_inode_bitmap);
			return 0;
		}
		if (gdp->bg_inode_table < min_blk ||
		    gdp->bg_inode_table + sb->u.ext2_sb.s_itb_per_group > max_blk) {
			ext4_error(sb, "ext4_check_descriptors",
				   "Inode table for group %d out of range (block %lu)!",
				   i, (unsigned long)gdp->bg_inode_table);
			return 0;
		}
		group_first += EXT2_BLOCKS_PER_GROUP(sb);
		gdp++;
	}
	return 1;
}

#define log2(n) ffz(~(n))

struct super_block *ext4_read_super(struct super_block *sb, void *data, int silent)
{
	struct buffer_head *bh;
	struct ext2_super_block *es;
	unsigned long sb_block = 1;
	unsigned short resuid = EXT2_DEF_RESUID;
	unsigned short resgid = EXT2_DEF_RESGID;
	unsigned long logic_sb_block = 1;
	kdev_t dev = sb->s_dev;
	int db_count;
	int i, j;

	sb->u.ext2_sb.s_mount_opt = 0;
	set_opt(sb->u.ext2_sb.s_mount_opt, CHECK_NORMAL);

	MOD_INC_USE_COUNT;
	lock_super(sb);
	set_blocksize(dev, BLOCK_SIZE);
	if (!(bh = bread(dev, sb_block, BLOCK_SIZE))) {
		sb->s_dev = 0;
		unlock_super(sb);
		printk("EXT4-fs: unable to read superblock\n");
		MOD_DEC_USE_COUNT;
		return NULL;
	}

	es = (struct ext2_super_block *)bh->b_data;
	sb->u.ext2_sb.s_es = es;
	sb->s_magic = es->s_magic;
	if (sb->s_magic != EXT2_SUPER_MAGIC) {
	failed_mount:
		sb->s_dev = 0;
		unlock_super(sb);
		if (bh)
			brelse(bh);
		MOD_DEC_USE_COUNT;
		return NULL;
	}

	/*
	 * Only claim filesystems that have EXT4_FEATURE_INCOMPAT_EXTENTS set,
	 * leaving legacy rev-0 ext2 images to fs/ext2.
	 */
	if (es->s_rev_level == EXT2_GOOD_OLD_REV ||
	    !(es->s_feature_incompat & EXT4_FEATURE_INCOMPAT_EXTENTS)) {
		goto failed_mount;
	}

	if (es->s_feature_incompat & ~EXT4_FEATURE_INCOMPAT_SUPP) {
		printk("EXT4-fs: %s: unsupported incompat features 0x%x\n",
		       kdevname(dev),
		       (unsigned int)(es->s_feature_incompat & ~EXT4_FEATURE_INCOMPAT_SUPP));
		goto failed_mount;
	}
	if (!(sb->s_flags & MS_RDONLY) &&
	    (es->s_feature_ro_compat & ~EXT4_FEATURE_RO_COMPAT_SUPP)) {
		printk("EXT4-fs: %s: unsupported ro_compat features 0x%x\n",
		       kdevname(dev),
		       (unsigned int)(es->s_feature_ro_compat & ~EXT4_FEATURE_RO_COMPAT_SUPP));
		goto failed_mount;
	}

	sb->s_blocksize_bits = sb->u.ext2_sb.s_es->s_log_block_size + 10;
	sb->s_blocksize = 1 << sb->s_blocksize_bits;
	if (sb->s_blocksize != BLOCK_SIZE &&
	    (sb->s_blocksize == 1024 || sb->s_blocksize == 2048 ||
	     sb->s_blocksize == 4096)) {
		unsigned long offset;

		brelse(bh);
		set_blocksize(dev, sb->s_blocksize);
		logic_sb_block = (sb_block * BLOCK_SIZE) / sb->s_blocksize;
		offset = (sb_block * BLOCK_SIZE) % sb->s_blocksize;
		bh = bread(dev, logic_sb_block, sb->s_blocksize);
		if (!bh) {
			printk("EXT4-fs: Couldn't read superblock on 2nd try.\n");
			goto failed_mount;
		}
		es = (struct ext2_super_block *)(((char *)bh->b_data) + offset);
		sb->u.ext2_sb.s_es = es;
		if (es->s_magic != EXT2_SUPER_MAGIC)
			goto failed_mount;
	}

	sb->u.ext2_sb.s_inode_size = es->s_inode_size ? es->s_inode_size : 128;
	sb->u.ext2_sb.s_first_ino = es->s_first_ino ? es->s_first_ino : 11;
	if (sb->u.ext2_sb.s_inode_size < 128 ||
	    sb->u.ext2_sb.s_inode_size > (int)sb->s_blocksize ||
	    (sb->u.ext2_sb.s_inode_size & (sb->u.ext2_sb.s_inode_size - 1)) != 0) {
		printk("EXT4-fs: unsupported inode size: %d\n",
		       sb->u.ext2_sb.s_inode_size);
		goto failed_mount;
	}

	sb->u.ext2_sb.s_frag_size = EXT2_MIN_FRAG_SIZE << es->s_log_frag_size;
	if (sb->u.ext2_sb.s_frag_size)
		sb->u.ext2_sb.s_frags_per_block = sb->s_blocksize /
						  sb->u.ext2_sb.s_frag_size;
	else
		sb->s_magic = 0;
	sb->u.ext2_sb.s_blocks_per_group = es->s_blocks_per_group;
	sb->u.ext2_sb.s_frags_per_group = es->s_frags_per_group;
	sb->u.ext2_sb.s_inodes_per_group = es->s_inodes_per_group;
	sb->u.ext2_sb.s_inodes_per_block = sb->s_blocksize / EXT2_INODE_SIZE(sb);
	sb->u.ext2_sb.s_itb_per_group = sb->u.ext2_sb.s_inodes_per_group /
					sb->u.ext2_sb.s_inodes_per_block;
	sb->u.ext2_sb.s_desc_per_block = sb->s_blocksize /
					 sizeof(struct ext2_group_desc);
	sb->u.ext2_sb.s_sbh = bh;
	sb->u.ext2_sb.s_resuid = (resuid != EXT2_DEF_RESUID) ? resuid : es->s_def_resuid;
	sb->u.ext2_sb.s_resgid = (resgid != EXT2_DEF_RESGID) ? resgid : es->s_def_resgid;
	sb->u.ext2_sb.s_mount_state = es->s_state;
	sb->u.ext2_sb.s_rename_lock = 0;
	sb->u.ext2_sb.s_rename_wait = NULL;
	sb->u.ext2_sb.s_addr_per_block_bits = log2(EXT2_ADDR_PER_BLOCK(sb));
	sb->u.ext2_sb.s_desc_per_block_bits = log2(EXT2_DESC_PER_BLOCK(sb));

	sb->u.ext2_sb.s_groups_count = (es->s_blocks_count -
					es->s_first_data_block +
					EXT2_BLOCKS_PER_GROUP(sb) - 1) /
				       EXT2_BLOCKS_PER_GROUP(sb);
	db_count = (sb->u.ext2_sb.s_groups_count + EXT2_DESC_PER_BLOCK(sb) - 1) /
		   EXT2_DESC_PER_BLOCK(sb);
	sb->u.ext2_sb.s_group_desc = kmalloc(db_count * sizeof(struct buffer_head *),
					     GFP_KERNEL);
	if (sb->u.ext2_sb.s_group_desc == NULL) {
		printk("EXT4-fs: not enough memory\n");
		goto failed_mount;
	}
	for (i = 0; i < db_count; i++) {
		sb->u.ext2_sb.s_group_desc[i] = bread(dev, logic_sb_block + i + 1,
						      sb->s_blocksize);
		if (!sb->u.ext2_sb.s_group_desc[i]) {
			for (j = 0; j < i; j++)
				brelse(sb->u.ext2_sb.s_group_desc[j]);
			kfree_s(sb->u.ext2_sb.s_group_desc,
				db_count * sizeof(struct buffer_head *));
			printk("EXT4-fs: unable to read group descriptors\n");
			goto failed_mount;
		}
	}
	if (!ext4_check_descriptors(sb)) {
		for (j = 0; j < db_count; j++)
			brelse(sb->u.ext2_sb.s_group_desc[j]);
		kfree_s(sb->u.ext2_sb.s_group_desc,
			db_count * sizeof(struct buffer_head *));
		printk("EXT4-fs: group descriptors corrupted!\n");
		goto failed_mount;
	}
	for (i = 0; i < EXT2_MAX_GROUP_LOADED; i++) {
		sb->u.ext2_sb.s_inode_bitmap_number[i] = 0;
		sb->u.ext2_sb.s_inode_bitmap[i] = NULL;
		sb->u.ext2_sb.s_block_bitmap_number[i] = 0;
		sb->u.ext2_sb.s_block_bitmap[i] = NULL;
	}
	sb->u.ext2_sb.s_loaded_inode_bitmaps = 0;
	sb->u.ext2_sb.s_loaded_block_bitmaps = 0;
	sb->u.ext2_sb.s_db_per_group = db_count;
	unlock_super(sb);

	sb->s_op = &ext4_sops;
	if (!(sb->s_mounted = iget(sb, EXT2_ROOT_INO))) {
		sb->s_dev = 0;
		for (i = 0; i < db_count; i++)
			if (sb->u.ext2_sb.s_group_desc[i])
				brelse(sb->u.ext2_sb.s_group_desc[i]);
		kfree_s(sb->u.ext2_sb.s_group_desc,
			db_count * sizeof(struct buffer_head *));
		brelse(bh);
		printk("EXT4-fs: get root inode failed\n");
		MOD_DEC_USE_COUNT;
		return NULL;
	}
	if (!S_ISDIR(sb->s_mounted->i_mode) || !sb->s_mounted->i_blocks ||
	    !sb->s_mounted->i_size) {
		iput(sb->s_mounted);
		sb->s_dev = 0;
		for (i = 0; i < db_count; i++)
			if (sb->u.ext2_sb.s_group_desc[i])
				brelse(sb->u.ext2_sb.s_group_desc[i]);
		kfree_s(sb->u.ext2_sb.s_group_desc,
			db_count * sizeof(struct buffer_head *));
		brelse(bh);
		printk("EXT4-fs: corrupt root inode\n");
		MOD_DEC_USE_COUNT;
		return NULL;
	}

	if (!(sb->s_flags & MS_RDONLY)) {
		es->s_mtime = CURRENT_TIME;
		mark_buffer_dirty(bh, 1);
		sb->s_dirt = 1;
	}

	printk("EXT4-fs (%s): mounted filesystem with ordered data mode "
	       "(inode_size=%d, features=extents%s%s%s).\n",
	       kdevname(dev),
	       sb->u.ext2_sb.s_inode_size,
	       (es->s_feature_incompat & EXT4_FEATURE_INCOMPAT_FLEX_BG) ? ",flex_bg" : "",
	       (es->s_feature_compat & EXT4_FEATURE_COMPAT_DIR_INDEX) ? ",dir_index" : "",
	       (es->s_feature_compat & EXT4_FEATURE_COMPAT_HAS_JOURNAL) ? ",has_journal" : "");

	return sb;
}

static void ext4_commit_super(struct super_block *sb, struct ext2_super_block *es)
{
	es->s_wtime = CURRENT_TIME;
	mark_buffer_dirty(sb->u.ext2_sb.s_sbh, 1);
	sb->s_dirt = 0;
}

void ext4_write_super(struct super_block *sb)
{
	struct ext2_super_block *es;

	if (!(sb->s_flags & MS_RDONLY)) {
		es = sb->u.ext2_sb.s_es;
		es->s_state = sb->u.ext2_sb.s_mount_state;
		ext4_commit_super(sb, es);
	}
	sb->s_dirt = 0;
}

int ext4_remount(struct super_block *sb, int *flags, char *data)
{
	struct ext2_super_block *es;

	es = sb->u.ext2_sb.s_es;
	if ((*flags & MS_RDONLY) == (sb->s_flags & MS_RDONLY))
		return 0;
	if (*flags & MS_RDONLY) {
		sb->u.ext2_sb.s_es->s_state = sb->u.ext2_sb.s_mount_state;
		mark_buffer_dirty(sb->u.ext2_sb.s_sbh, 1);
		sb->s_dirt = 1;
		ext4_commit_super(sb, es);
	} else {
		sb->u.ext2_sb.s_mount_state = es->s_state;
		es->s_mtime = CURRENT_TIME;
		mark_buffer_dirty(sb->u.ext2_sb.s_sbh, 1);
		sb->s_dirt = 1;
	}
	return 0;
}

void ext4_statfs(struct super_block *sb, struct statfs *buf, int bufsiz)
{
	unsigned long overhead;
	struct statfs tmp;

	if (sb->u.ext2_sb.s_es->s_feature_incompat & EXT4_FEATURE_INCOMPAT_FLEX_BG) {
		overhead = sb->u.ext2_sb.s_es->s_first_data_block +
			   sb->u.ext2_sb.s_groups_count *
			   (2 + sb->u.ext2_sb.s_itb_per_group);
	} else {
		overhead = sb->u.ext2_sb.s_es->s_first_data_block +
			   sb->u.ext2_sb.s_groups_count *
			   (1 + sb->u.ext2_sb.s_db_per_group +
			    2 + sb->u.ext2_sb.s_itb_per_group);
	}
	tmp.f_type = EXT2_SUPER_MAGIC;
	tmp.f_bsize = sb->s_blocksize;
	tmp.f_blocks = (sb->u.ext2_sb.s_es->s_blocks_count > overhead) ?
		       (sb->u.ext2_sb.s_es->s_blocks_count - overhead) :
		       sb->u.ext2_sb.s_es->s_blocks_count;
	tmp.f_bfree = ext4_count_free_blocks(sb);
	tmp.f_bavail = tmp.f_bfree;
	if (tmp.f_bfree >= sb->u.ext2_sb.s_es->s_r_blocks_count)
		tmp.f_bavail -= sb->u.ext2_sb.s_es->s_r_blocks_count;
	else
		tmp.f_bavail = 0;
	tmp.f_files = sb->u.ext2_sb.s_es->s_inodes_count;
	tmp.f_ffree = ext4_count_free_inodes(sb);
	tmp.f_namelen = EXT2_NAME_LEN;
	memcpy_tofs(buf, &tmp, bufsiz);
}

static struct file_system_type ext4_fs_type = {
	ext4_read_super, "ext4", 1, NULL
};

int init_ext4_fs(void)
{
	return register_filesystem(&ext4_fs_type);
}
