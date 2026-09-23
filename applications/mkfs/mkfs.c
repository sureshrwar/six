/*
 * mkfs.ext2 (mke2fs) - In-guest ext2 filesystem formatter for SIX
 *
 * Formats any block device (/dev/hdc, /dev/mapper/linear0, /dev/mapper/crypt0,
 * etc.) or disk image file with a revision-0 ext2 filesystem (1024-byte blocks,
 * 128-byte inodes).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/ioctl.h>

#define BLKGETSIZE		0x1260
#define BLKFLSBUF		0x1261

#define EXT2_SUPER_MAGIC	0xEF53
#define EXT2_VALID_FS		1
#define EXT2_ERRORS_CONTINUE	1
#define EXT2_GOOD_OLD_REV	0
#define EXT2_GOOD_OLD_INODE_SIZE 128
#define EXT2_GOOD_OLD_FIRST_INO	11
#define EXT2_ROOT_INO		2

#define BLOCK_SIZE		1024
#define BLOCKS_PER_GROUP	8192
#define INODES_PER_GROUP	512
#define INODE_TABLE_BLOCKS	((INODES_PER_GROUP * EXT2_GOOD_OLD_INODE_SIZE) / BLOCK_SIZE) /* 64 */
#define MAX_GROUPS		32

struct ext2_super_block {
	unsigned int   s_inodes_count;
	unsigned int   s_blocks_count;
	unsigned int   s_r_blocks_count;
	unsigned int   s_free_blocks_count;
	unsigned int   s_free_inodes_count;
	unsigned int   s_first_data_block;
	unsigned int   s_log_block_size;
	int            s_log_frag_size;
	unsigned int   s_blocks_per_group;
	unsigned int   s_frags_per_group;
	unsigned int   s_inodes_per_group;
	unsigned int   s_mtime;
	unsigned int   s_wtime;
	unsigned short s_mnt_count;
	short          s_max_mnt_count;
	unsigned short s_magic;
	unsigned short s_state;
	unsigned short s_errors;
	unsigned short s_minor_rev_level;
	unsigned int   s_lastcheck;
	unsigned int   s_checkinterval;
	unsigned int   s_creator_os;
	unsigned int   s_rev_level;
	unsigned short s_def_resuid;
	unsigned short s_def_resgid;
	unsigned int   s_first_ino;
	unsigned short s_inode_size;
	unsigned short s_block_group_nr;
	unsigned int   s_feature_compat;
	unsigned int   s_feature_incompat;
	unsigned int   s_feature_ro_compat;
	unsigned char  s_uuid[16];
	char           s_volume_name[16];
	char           s_last_mounted[64];
	unsigned int   s_reserved[206];
};

struct ext2_group_desc {
	unsigned int   bg_block_bitmap;
	unsigned int   bg_inode_bitmap;
	unsigned int   bg_inode_table;
	unsigned short bg_free_blocks_count;
	unsigned short bg_free_inodes_count;
	unsigned short bg_used_dirs_count;
	unsigned short bg_pad;
	unsigned int   bg_reserved[3];
};

struct ext2_inode {
	unsigned short i_mode;
	unsigned short i_uid;
	unsigned int   i_size;
	unsigned int   i_atime;
	unsigned int   i_ctime;
	unsigned int   i_mtime;
	unsigned int   i_dtime;
	unsigned short i_gid;
	unsigned short i_links_count;
	unsigned int   i_blocks;
	unsigned int   i_flags;
	unsigned int   i_osd1;
	unsigned int   i_block[15];
	unsigned int   i_version;
	unsigned int   i_file_acl;
	unsigned int   i_dir_acl;
	unsigned int   i_faddr;
	unsigned char  i_osd2[12];
};

struct ext2_dir_entry {
	unsigned int   inode;
	unsigned short rec_len;
	unsigned short name_len;
	char           name[4];
};

static int write_block(int fd, unsigned long block_nr, const void *buf)
{
	if (lseek(fd, (long)block_nr * BLOCK_SIZE, 0) < 0)
		return -1;
	if (write(fd, buf, BLOCK_SIZE) != BLOCK_SIZE)
		return -1;
	return 0;
}

static void set_bitmap_bit(unsigned char *bm, int bit)
{
	bm[bit >> 3] |= (unsigned char)(1 << (bit & 7));
}

int main(int argc, char *argv[])
{
	const char *dev_path = NULL;
	const char *label = "";
	int i, fd;
	unsigned long sectors = 0, total_blocks = 0;
	unsigned int groups_count, g;
	unsigned int total_free_blocks = 0, total_free_inodes = 0;
	unsigned int now;
	struct ext2_super_block sb;
	struct ext2_group_desc gdt[MAX_GROUPS];
	unsigned char block_buf[BLOCK_SIZE];

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-L") == 0 && i + 1 < argc) {
			label = argv[++i];
		} else if (argv[i][0] == '-') {
			continue;
		} else if (!dev_path) {
			dev_path = argv[i];
		} else {
			total_blocks = (unsigned long)atol(argv[i]);
		}
	}

	if (!dev_path) {
		printf("Usage: mkfs.ext2 [-L label] <device> [blocks]\n");
		return 1;
	}

	fd = open(dev_path, O_RDWR);
	if (fd < 0) {
		printf("mkfs.ext2: cannot open %s\n", dev_path);
		return 1;
	}

	if (total_blocks == 0) {
		if (ioctl(fd, BLKGETSIZE, &sectors) == 0 && sectors > 0) {
			total_blocks = sectors >> 1;
		} else {
			long bytes = lseek(fd, 0L, 2);
			lseek(fd, 0L, 0);
			if (bytes > 0)
				total_blocks = (unsigned long)(bytes / BLOCK_SIZE);
		}
	}

	if (total_blocks < 128) {
		printf("mkfs.ext2: device %s too small (%lu blocks)\n", dev_path, total_blocks);
		close(fd);
		return 1;
	}

	groups_count = (unsigned int)((total_blocks - 1 + BLOCKS_PER_GROUP - 1) / BLOCKS_PER_GROUP);
	if (groups_count > MAX_GROUPS) {
		groups_count = MAX_GROUPS;
		total_blocks = 1 + (unsigned long)groups_count * BLOCKS_PER_GROUP;
	}

	now = (unsigned int)time(NULL);
	memset(gdt, 0, sizeof(gdt));

	/* Initialize each block group */
	for (g = 0; g < groups_count; g++) {
		unsigned int group_start = 1 + g * BLOCKS_PER_GROUP;
		unsigned int group_end = group_start + BLOCKS_PER_GROUP;
		unsigned int group_blocks, meta_blocks, free_blocks, free_inodes;
		unsigned int b, bit;

		if (group_end > (unsigned int)total_blocks)
			group_end = (unsigned int)total_blocks;
		group_blocks = group_end - group_start;

		/*
		 * Layout per group:
		 *   +0: superblock (or backup)
		 *   +1: group descriptor table (1 block)
		 *   +2: block bitmap (1 block)
		 *   +3: inode bitmap (1 block)
		 *   +4..+67: inode table (64 blocks)
		 *   +68 (group 0 only): root directory data block
		 */
		meta_blocks = 4 + INODE_TABLE_BLOCKS + (g == 0 ? 1 : 0);
		if (group_blocks <= meta_blocks) {
			printf("mkfs.ext2: group %u too small\n", g);
			close(fd);
			return 1;
		}
		free_blocks = group_blocks - meta_blocks;
		free_inodes = INODES_PER_GROUP - (g == 0 ? EXT2_GOOD_OLD_FIRST_INO : 0);

		gdt[g].bg_block_bitmap = group_start + 2;
		gdt[g].bg_inode_bitmap = group_start + 3;
		gdt[g].bg_inode_table  = group_start + 4;
		gdt[g].bg_free_blocks_count = (unsigned short)free_blocks;
		gdt[g].bg_free_inodes_count = (unsigned short)free_inodes;
		gdt[g].bg_used_dirs_count   = (g == 0) ? 1 : 0;

		total_free_blocks += free_blocks;
		total_free_inodes += free_inodes;

		/* Write block bitmap */
		memset(block_buf, 0, BLOCK_SIZE);
		for (bit = 0; bit < meta_blocks; bit++)
			set_bitmap_bit(block_buf, bit);
		for (bit = group_blocks; bit < BLOCKS_PER_GROUP; bit++)
			set_bitmap_bit(block_buf, bit);
		write_block(fd, gdt[g].bg_block_bitmap, block_buf);

		/* Write inode bitmap */
		memset(block_buf, 0, BLOCK_SIZE);
		if (g == 0) {
			for (bit = 0; bit < EXT2_GOOD_OLD_FIRST_INO; bit++)
				set_bitmap_bit(block_buf, bit);
		}
		for (bit = INODES_PER_GROUP; bit < BLOCKS_PER_GROUP; bit++)
			set_bitmap_bit(block_buf, bit);
		write_block(fd, gdt[g].bg_inode_bitmap, block_buf);

		/* Zero inode table blocks */
		memset(block_buf, 0, BLOCK_SIZE);
		for (b = 0; b < INODE_TABLE_BLOCKS; b++) {
			if (g == 0 && b == 0) {
				struct ext2_inode *inodes = (struct ext2_inode *)block_buf;
				/* Inode 2 is index 1 (EXT2_ROOT_INO - 1) */
				inodes[EXT2_ROOT_INO - 1].i_mode = 0040755;
				inodes[EXT2_ROOT_INO - 1].i_uid = 0;
				inodes[EXT2_ROOT_INO - 1].i_size = BLOCK_SIZE;
				inodes[EXT2_ROOT_INO - 1].i_atime = now;
				inodes[EXT2_ROOT_INO - 1].i_ctime = now;
				inodes[EXT2_ROOT_INO - 1].i_mtime = now;
				inodes[EXT2_ROOT_INO - 1].i_gid = 0;
				inodes[EXT2_ROOT_INO - 1].i_links_count = 2;
				inodes[EXT2_ROOT_INO - 1].i_blocks = BLOCK_SIZE / 512;
				inodes[EXT2_ROOT_INO - 1].i_block[0] = group_start + 4 + INODE_TABLE_BLOCKS;
				write_block(fd, gdt[g].bg_inode_table + b, block_buf);
				memset(block_buf, 0, BLOCK_SIZE);
			} else {
				write_block(fd, gdt[g].bg_inode_table + b, block_buf);
			}
		}

		/* Write root directory block in group 0 */
		if (g == 0) {
			struct ext2_dir_entry *de;
			unsigned int root_blk = group_start + 4 + INODE_TABLE_BLOCKS;

			memset(block_buf, 0, BLOCK_SIZE);
			de = (struct ext2_dir_entry *)block_buf;
			de->inode = EXT2_ROOT_INO;
			de->rec_len = 12;
			de->name_len = 1;
			de->name[0] = '.';

			de = (struct ext2_dir_entry *)(block_buf + 12);
			de->inode = EXT2_ROOT_INO;
			de->rec_len = BLOCK_SIZE - 12;
			de->name_len = 2;
			de->name[0] = '.';
			de->name[1] = '.';

			write_block(fd, root_blk, block_buf);
		}
	}

	/* Populate Superblock */
	memset(&sb, 0, sizeof(sb));
	sb.s_inodes_count      = groups_count * INODES_PER_GROUP;
	sb.s_blocks_count      = (unsigned int)total_blocks;
	sb.s_r_blocks_count    = (unsigned int)(total_blocks / 20);
	sb.s_free_blocks_count = total_free_blocks;
	sb.s_free_inodes_count = total_free_inodes;
	sb.s_first_data_block  = 1;
	sb.s_log_block_size    = 0; /* 1024-byte blocks */
	sb.s_log_frag_size     = 0;
	sb.s_blocks_per_group  = BLOCKS_PER_GROUP;
	sb.s_frags_per_group   = BLOCKS_PER_GROUP;
	sb.s_inodes_per_group  = INODES_PER_GROUP;
	sb.s_mtime             = now;
	sb.s_wtime             = now;
	sb.s_mnt_count         = 0;
	sb.s_max_mnt_count     = 20;
	sb.s_magic             = EXT2_SUPER_MAGIC;
	sb.s_state             = EXT2_VALID_FS;
	sb.s_errors            = EXT2_ERRORS_CONTINUE;
	sb.s_lastcheck         = now;
	sb.s_checkinterval     = 0;
	sb.s_creator_os        = 0;
	sb.s_rev_level         = EXT2_GOOD_OLD_REV;
	sb.s_first_ino         = EXT2_GOOD_OLD_FIRST_INO;
	sb.s_inode_size        = EXT2_GOOD_OLD_INODE_SIZE;
	strncpy(sb.s_volume_name, label, 15);

	/* Write boot block 0, primary/backup superblocks, and group descriptor tables */
	memset(block_buf, 0, BLOCK_SIZE);
	write_block(fd, 0, block_buf);

	for (g = 0; g < groups_count; g++) {
		unsigned int group_start = 1 + g * BLOCKS_PER_GROUP;
		sb.s_block_group_nr = (unsigned short)g;
		write_block(fd, group_start, &sb);
		write_block(fd, group_start + 1, gdt);
	}

	ioctl(fd, BLKFLSBUF, 0);
	close(fd);

	printf("mke2fs %s: %lu blocks (%lu KB), %u block groups, %u free blocks, %u free inodes\n",
	       dev_path, total_blocks, total_blocks, groups_count,
	       total_free_blocks, total_free_inodes);
	return 0;
}
