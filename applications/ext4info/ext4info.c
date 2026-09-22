/*
 * ext4info.c - Live in-guest ext4 inode & extent B+tree inspector for SIX
 *
 * Queries the SIX fs/ext4 kernel driver via EXT4_IOC_GET_INFO (0x6610)
 * to display the live on-disk ext4 metadata, 256-byte inode location,
 * feature bitmasks, and extent B+tree mapping of any file or directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/fcntl.h>
#include <linux/ext4_extents.h>

extern int open(const char *pathname, int flags, ...);
extern int close(int fd);
extern int read(int fd, void *buf, int count);
extern int ioctl(int fd, int request, ...);

static int detect_fs_type(const char *dev_path)
{
	unsigned char buf[2048];
	int fd, n;
	unsigned short s_magic;
	unsigned int s_rev_level;
	unsigned int s_feature_incompat;

	fd = open(dev_path, O_RDONLY);
	if (fd < 0) {
		printf("none\n");
		return 1;
	}

	memset(buf, 0, sizeof(buf));
	n = read(fd, buf, sizeof(buf));
	close(fd);

	if (n >= 512 && memcmp(buf + 3, "NTFS    ", 8) == 0) {
		printf("NTFS\n");
		return 0;
	}

	if (n >= 1024 + 100) {
		s_magic = *(unsigned short *)(buf + 1024 + 0x38);
		s_rev_level = *(unsigned int *)(buf + 1024 + 0x4c);
		s_feature_incompat = *(unsigned int *)(buf + 1024 + 0x60);
		if (s_magic == 0xEF53) {
			if (s_rev_level != 0 && (s_feature_incompat & 0x0040))
				printf("ext4\n");
			else
				printf("ext2\n");
			return 0;
		}
	}

	printf("unknown\n");
	return 2;
}

static void print_incompat_flags(unsigned int f)
{
	int first = 1;
	struct { unsigned int bit; const char *name; } tab[] = {
		{ 0x0002, "filetype" },
		{ 0x0004, "recover" },
		{ 0x0010, "meta_bg" },
		{ 0x0040, "extent" },
		{ 0x0080, "64bit" },
		{ 0x0200, "flex_bg" },
		{ 0, NULL }
	};
	int i;
	for (i = 0; tab[i].name; i++) {
		if (f & tab[i].bit) {
			printf("%s%s", first ? "" : ",", tab[i].name);
			first = 0;
		}
	}
	if (first) printf("none");
}

static void print_inode_flags(unsigned int f)
{
	int first = 1;
	struct { unsigned int bit; const char *name; } tab[] = {
		{ 0x00000008, "SYNC" },
		{ 0x00000010, "IMMUTABLE" },
		{ 0x00000020, "APPEND" },
		{ 0x00001000, "EXT4_INDEX_FL(htree)" },
		{ 0x00040000, "EXT4_HUGE_FILE_FL" },
		{ 0x00080000, "EXT4_EXTENTS_FL" },
		{ 0, NULL }
	};
	int i;
	for (i = 0; tab[i].name; i++) {
		if (f & tab[i].bit) {
			printf("%s%s", first ? "" : " | ", tab[i].name);
			first = 0;
		}
	}
	if (first) printf("0x0");
}

static int inspect_path(const char *path)
{
	struct ext4_inode_inspect info;
	int fd;
	unsigned int i;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		printf("ext4info: cannot open '%s'\n", path);
		return 1;
	}

	memset(&info, 0, sizeof(info));
	if (ioctl(fd, EXT4_IOC_GET_INFO, &info) < 0) {
		printf("ext4info: '%s' is not on an ext4 filesystem (or ioctl unsupported)\n", path);
		close(fd);
		return 1;
	}
	close(fd);

	printf("=== EXT4 On-Disk Inode & Extent Tree: %s ===\n", path);
	printf("  Inode Number     : %u  (Block Group %u)\n",
	       info.ino, info.block_group);
	printf("  Inode Table Loc  : phys_block=%u, byte_offset=%u (inode_size=%u, extra_isize=%u)\n",
	       info.inode_table_block, info.inode_block_offset,
	       info.inode_size, info.extra_isize);
	printf("  File Size        : %u bytes (%u x 512B sectors)\n",
	       info.i_size, info.i_blocks);
	printf("  Inode Flags      : 0x%08x (", info.i_flags);
	print_inode_flags(info.i_flags);
	printf(")\n");
	printf("  FS Incompat Feat : 0x%08x (", info.s_feature_incompat);
	print_incompat_flags(info.s_feature_incompat);
	printf(")\n");

	if (info.i_flags & 0x00080000) {
		printf("  Extent Header    : eh_magic=0x%04x, eh_depth=%u, eh_entries=%u/%u\n",
		       info.eh_magic, info.eh_depth, info.eh_entries, info.eh_max);
		if (info.num_returned_extents > 0) {
			printf("  Extent Map       :\n");
			for (i = 0; i < info.num_returned_extents; i++) {
				unsigned int l_start = info.extents[i].ee_block;
				unsigned int len = info.extents[i].ee_len;
				unsigned int p_start = info.extents[i].ee_start_lo;
				printf("    [%2u] logical [%4u .. %4u] (%3u blk) -> physical [%5u .. %5u]\n",
				       i, l_start, l_start + len - 1, len,
				       p_start, p_start + len - 1);
			}
		} else {
			printf("  Extent Map       : (empty / 0 blocks allocated)\n");
		}
	} else {
		printf("  Block Mapping    : Legacy direct/indirect blocks\n");
	}
	return 0;
}

int main(int argc, char **argv)
{
	int i, rc = 0;

	if (argc == 3 && strcmp(argv[1], "-t") == 0)
		return detect_fs_type(argv[2]);

	if (argc < 2) {
		printf("Usage: ext4info [-t <dev>] <path> [path2 ...]\n");
		printf("Inspect live on-disk ext4 256-byte inode & extent B+tree structures.\n");
		return 1;
	}

	for (i = 1; i < argc; i++) {
		if (i > 1) printf("\n");
		rc |= inspect_path(argv[i]);
	}
	return rc;
}
