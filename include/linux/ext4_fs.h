/*
 * include/linux/ext4_fs.h
 *
 * Definitions, feature flags, HTree structs, and function prototypes
 * for SIX's fs/ext4 filesystem driver.
 */

#ifndef _LINUX_EXT4_FS_H
#define _LINUX_EXT4_FS_H

#include <linux/types.h>
#include <linux/ext2_fs.h>
#include <linux/ext4_extents.h>

/*
 * EXT4 Inode flags (i_flags)
 */
#define EXT4_INDEX_FL			0x00001000 /* hash-indexed directory (htree) */
#define EXT4_HUGE_FILE_FL		0x00040000 /* Set to each huge file */
#define EXT4_EXTENTS_FL			0x00080000 /* Inode uses extents */
#define EXT4_EA_INODE_FL		0x00200000 /* Inode used for large EA */

/*
 * EXT4 Feature flags (s_feature_compat, s_feature_incompat, s_feature_ro_compat)
 */
#define EXT4_FEATURE_COMPAT_DIR_PREALLOC	0x0001
#define EXT4_FEATURE_COMPAT_IMAGIC_INODES	0x0002
#define EXT4_FEATURE_COMPAT_HAS_JOURNAL		0x0004
#define EXT4_FEATURE_COMPAT_EXT_ATTR		0x0008
#define EXT4_FEATURE_COMPAT_RESIZE_INODE	0x0010
#define EXT4_FEATURE_COMPAT_DIR_INDEX		0x0020
#define EXT4_FEATURE_COMPAT_SPARSE_SUPER2	0x0200

#define EXT4_FEATURE_RO_COMPAT_SPARSE_SUPER	0x0001
#define EXT4_FEATURE_RO_COMPAT_LARGE_FILE	0x0002
#define EXT4_FEATURE_RO_COMPAT_BTREE_DIR	0x0004
#define EXT4_FEATURE_RO_COMPAT_HUGE_FILE	0x0008
#define EXT4_FEATURE_RO_COMPAT_GDT_CSUM		0x0010
#define EXT4_FEATURE_RO_COMPAT_DIR_NLINK	0x0020
#define EXT4_FEATURE_RO_COMPAT_EXTRA_ISIZE	0x0040
#define EXT4_FEATURE_RO_COMPAT_METADATA_CSUM	0x0400
#define EXT4_FEATURE_RO_COMPAT_ORPHAN_PRESENT	0x1000

#define EXT4_FEATURE_INCOMPAT_COMPRESSION	0x0001
#define EXT4_FEATURE_INCOMPAT_FILETYPE		0x0002
#define EXT4_FEATURE_INCOMPAT_RECOVER		0x0004
#define EXT4_FEATURE_INCOMPAT_JOURNAL_DEV	0x0008
#define EXT4_FEATURE_INCOMPAT_META_BG		0x0010
#define EXT4_FEATURE_INCOMPAT_EXTENTS		0x0040
#define EXT4_FEATURE_INCOMPAT_64BIT		0x0080
#define EXT4_FEATURE_INCOMPAT_MMP		0x0100
#define EXT4_FEATURE_INCOMPAT_FLEX_BG		0x0200
#define EXT4_FEATURE_INCOMPAT_LARGEDIR		0x4000
#define EXT4_FEATURE_INCOMPAT_INLINE_DATA	0x8000

#define EXT4_FEATURE_INCOMPAT_SUPP \
	(EXT4_FEATURE_INCOMPAT_FILETYPE | \
	 EXT4_FEATURE_INCOMPAT_RECOVER | \
	 EXT4_FEATURE_INCOMPAT_META_BG | \
	 EXT4_FEATURE_INCOMPAT_EXTENTS | \
	 EXT4_FEATURE_INCOMPAT_64BIT | \
	 EXT4_FEATURE_INCOMPAT_FLEX_BG)

#define EXT4_FEATURE_RO_COMPAT_SUPP \
	(EXT4_FEATURE_RO_COMPAT_SPARSE_SUPER | \
	 EXT4_FEATURE_RO_COMPAT_LARGE_FILE | \
	 EXT4_FEATURE_RO_COMPAT_BTREE_DIR | \
	 EXT4_FEATURE_RO_COMPAT_HUGE_FILE | \
	 EXT4_FEATURE_RO_COMPAT_GDT_CSUM | \
	 EXT4_FEATURE_RO_COMPAT_DIR_NLINK | \
	 EXT4_FEATURE_RO_COMPAT_EXTRA_ISIZE | \
	 EXT4_FEATURE_RO_COMPAT_METADATA_CSUM | \
	 EXT4_FEATURE_RO_COMPAT_ORPHAN_PRESENT)

/*
 * Directory entry file_type values (stored in high byte of de->name_len
 * when EXT4_FEATURE_INCOMPAT_FILETYPE is active).
 */
#define EXT4_FT_UNKNOWN		0
#define EXT4_FT_REG_FILE	1
#define EXT4_FT_DIR		2
#define EXT4_FT_CHRDEV		3
#define EXT4_FT_BLKDEV		4
#define EXT4_FT_FIFO		5
#define EXT4_FT_SOCK		6
#define EXT4_FT_SYMLINK		7
#define EXT4_FT_MAX		8

#define EXT4_DIR_NAMELEN(de)	((de)->name_len & 0xFF)
#define EXT4_DIR_FILETYPE(de)	(((de)->name_len >> 8) & 0xFF)

static inline __u8 ext4_type_by_mode(unsigned short mode)
{
	if (S_ISREG(mode))  return EXT4_FT_REG_FILE;
	if (S_ISDIR(mode))  return EXT4_FT_DIR;
	if (S_ISCHR(mode))  return EXT4_FT_CHRDEV;
	if (S_ISBLK(mode))  return EXT4_FT_BLKDEV;
	if (S_ISFIFO(mode)) return EXT4_FT_FIFO;
	if (S_ISSOCK(mode)) return EXT4_FT_SOCK;
	if (S_ISLNK(mode))  return EXT4_FT_SYMLINK;
	return EXT4_FT_UNKNOWN;
}

static inline void ext4_set_de_namelen(struct super_block *sb,
				       struct ext2_dir_entry *de,
				       int namelen, unsigned short mode)
{
	if (sb && (sb->u.ext2_sb.s_es->s_feature_incompat & EXT4_FEATURE_INCOMPAT_FILETYPE)) {
		de->name_len = (unsigned short)((namelen & 0xFF) |
			       ((unsigned short)ext4_type_by_mode(mode) << 8));
	} else {
		de->name_len = (unsigned short)(namelen & 0xFF);
	}
}

/*
 * HTree (dir_index) directory hash definitions
 */
#define DX_HASH_LEGACY			0
#define DX_HASH_HALF_MD4		1
#define DX_HASH_TEA			2
#define DX_HASH_LEGACY_UNSIGNED		3
#define DX_HASH_HALF_MD4_UNSIGNED	4
#define DX_HASH_TEA_UNSIGNED		5

struct dx_entry {
	__u32 hash;
	__u32 block;
};

struct dx_countlimit {
	__u16 limit;
	__u16 count;
};

#ifdef __KERNEL__

/* fs/ext4/super.c */
extern int init_ext4_fs(void);
extern struct super_block *ext4_read_super(struct super_block *sb, void *data, int silent);
extern void ext4_put_super(struct super_block *sb);
extern void ext4_write_super(struct super_block *sb);
extern int ext4_remount(struct super_block *sb, int *flags, char *data);
extern void ext4_statfs(struct super_block *sb, struct statfs *buf, int bufsiz);
extern void ext4_error(struct super_block *sb, const char *function, const char *fmt, ...);
extern NORET_TYPE void ext4_panic(struct super_block *sb, const char *function, const char *fmt, ...);
extern void ext4_warning(struct super_block *sb, const char *function, const char *fmt, ...);

/* fs/ext4/extents.c */
extern void ext4_ext_tree_init(struct inode *inode);
extern int ext4_ext_get_block(struct inode *inode, long iblock, int create, int *err);
extern struct buffer_head *ext4_ext_getblk(struct inode *inode, long block, int create, int *err);
extern void ext4_ext_truncate(struct inode *inode);
extern int ext4_ext_inspect(struct inode *inode, struct ext4_inode_inspect *info);

/* fs/ext4/htree.c */
extern __u32 ext4fs_dirhash(const char *name, int len, int hash_version, const __u32 *seed);
extern struct buffer_head *ext4_dx_find_entry(struct inode *dir, const char *name, int namelen,
					      struct ext2_dir_entry **res_dir);

/* fs/ext4/inode.c */
extern void ext4_read_inode(struct inode *inode);
extern void ext4_write_inode(struct inode *inode);
extern void ext4_put_inode(struct inode *inode);
extern int ext4_sync_inode(struct inode *inode);
extern int ext4_bmap(struct inode *inode, int block);
extern struct buffer_head *ext4_getblk(struct inode *inode, long block, int create, int *err);
extern struct buffer_head *ext4_bread(struct inode *inode, int block, int create, int *err);
extern void ext4_discard_prealloc(struct inode *inode);

/* fs/ext4/balloc.c & ialloc.c */
extern int ext4_new_block(const struct inode *inode, unsigned long goal,
			  __u32 *prealloc_count, __u32 *prealloc_block, int *err);
extern void ext4_free_blocks(const struct inode *inode, unsigned long block,
			     unsigned long count);
extern unsigned long ext4_count_free_blocks(struct super_block *sb);
extern int ext4_group_sparse(int group);
extern struct inode *ext4_new_inode(const struct inode *dir, int mode, int *err);
extern void ext4_free_inode(struct inode *inode);
extern unsigned long ext4_count_free_inodes(struct super_block *sb);
extern unsigned long ext4_count_free(struct buffer_head *map, unsigned int numchars);

/* fs/ext4/namei.c, dir.c, file.c, symlink.c, truncate.c, ioctl.c, fsync.c, acl.c */
extern int ext4_open(struct inode *inode, struct file *filp);
extern void ext4_release(struct inode *inode, struct file *filp);
extern int ext4_lookup(struct inode *dir, const char *name, int len, struct inode **result);
extern int ext4_create(struct inode *dir, const char *name, int len, int mode, struct inode **result);
extern int ext4_mkdir(struct inode *dir, const char *name, int len, int mode);
extern int ext4_rmdir(struct inode *dir, const char *name, int len);
extern int ext4_unlink(struct inode *dir, const char *name, int len);
extern int ext4_symlink(struct inode *dir, const char *name, int len, const char *symname);
extern int ext4_link(struct inode *oldinode, struct inode *dir, const char *name, int len);
extern int ext4_mknod(struct inode *dir, const char *name, int len, int mode, int rdev);
extern int ext4_rename(struct inode *old_dir, const char *old_name, int old_len,
		       struct inode *new_dir, const char *new_name, int new_len,
		       int must_be_dir);
extern int ext4_check_dir_entry(const char *function, struct inode *dir,
				struct ext2_dir_entry *de, struct buffer_head *bh,
				unsigned long offset);
extern void ext4_truncate(struct inode *inode);
extern int ext4_permission(struct inode *inode, int mask);
extern int ext4_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg);
extern int ext4_sync_file(struct inode *inode, struct file *file);

extern struct inode_operations ext4_file_inode_operations;
extern struct inode_operations ext4_dir_inode_operations;
extern struct inode_operations ext4_symlink_inode_operations;

#endif /* __KERNEL__ */
#endif /* _LINUX_EXT4_FS_H */
