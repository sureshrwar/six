/*
 * lsblk.c - List block devices and Device Mapper tree hierarchy for SIX
 *
 * Usage:
 *   lsblk [-f]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "../../include/linux/dm.h"

#define BLKROGET	0x125E
#define BLKGETSIZE	0x1260

struct mount_entry {
	char dev[64];
	char mnt[64];
	char fstype[16];
};

static struct mount_entry mounts[32];
static int num_mounts = 0;

static void load_mounts(void)
{
	FILE *fp = fopen("/proc/mounts", "r");
	char line[256];

	num_mounts = 0;
	if (!fp)
		return;
	while (fgets(line, sizeof(line), fp) && num_mounts < 32) {
		char *d = line, *m = NULL, *t = NULL, *p;
		while (*d == ' ' || *d == '\t')
			d++;
		p = d;
		while (*p && *p != ' ' && *p != '\t')
			p++;
		if (!*p)
			continue;
		*p++ = '\0';
		while (*p == ' ' || *p == '\t')
			p++;
		m = p;
		while (*p && *p != ' ' && *p != '\t')
			p++;
		if (!*p)
			continue;
		*p++ = '\0';
		while (*p == ' ' || *p == '\t')
			p++;
		t = p;
		while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
			p++;
		*p = '\0';

		strncpy(mounts[num_mounts].dev, d, sizeof(mounts[num_mounts].dev) - 1);
		strncpy(mounts[num_mounts].mnt, m, sizeof(mounts[num_mounts].mnt) - 1);
		strncpy(mounts[num_mounts].fstype, t, sizeof(mounts[num_mounts].fstype) - 1);
		num_mounts++;
	}
	fclose(fp);
}

static const char *find_mountpoint(const char *dev_path, const char *alt_path, int is_root_hda)
{
	int i;
	for (i = 0; i < num_mounts; i++) {
		if (strcmp(mounts[i].dev, dev_path) == 0)
			return mounts[i].mnt;
		if (alt_path && strcmp(mounts[i].dev, alt_path) == 0)
			return mounts[i].mnt;
		if (is_root_hda && strcmp(mounts[i].dev, "rootfs") == 0)
			return mounts[i].mnt;
	}
	return "";
}

static void probe_fs(const char *dev_path, char *fstype, char *label)
{
	unsigned char buf[2048];
	int fd, n;

	strcpy(fstype, "");
	strcpy(label, "");

	fd = open(dev_path, O_RDONLY);
	if (fd < 0)
		return;
	memset(buf, 0, sizeof(buf));
	n = read(fd, buf, sizeof(buf));
	close(fd);

	if (n >= 512 && memcmp(buf + 3, "NTFS    ", 8) == 0) {
		strcpy(fstype, "ntfs");
		return;
	}
	if (n >= 1024 + 0x50) {
		unsigned int erofs_magic = *(unsigned int *)(buf + 1024);
		if (erofs_magic == 0xE0F5E1E2U) {
			strcpy(fstype, "erofs");
			strncpy(label, (char *)(buf + 1024 + 0x40), 15);
			label[15] = '\0';
			return;
		}
	}
	if (n >= 1024 + 0x88) {
		unsigned short s_magic = *(unsigned short *)(buf + 1024 + 0x38);
		unsigned int s_rev = *(unsigned int *)(buf + 1024 + 0x4c);
		unsigned int s_incompat = *(unsigned int *)(buf + 1024 + 0x60);
		if (s_magic == 0xEF53) {
			if (s_rev != 0 && (s_incompat & 0x0040))
				strcpy(fstype, "ext4");
			else
				strcpy(fstype, "ext2");
			strncpy(label, (char *)(buf + 1024 + 0x78), 15);
			label[15] = '\0';
		}
	}
}

static void format_size(unsigned long sectors, char *out)
{
	unsigned long kb = sectors >> 1;
	if (kb >= 1024)
		sprintf(out, "%luM", kb / 1024);
	else
		sprintf(out, "%luK", kb);
}

static const char *dm_type_str(int type)
{
	switch (type) {
	case DM_TARGET_LINEAR:  return "linear";
	case DM_TARGET_CRYPT:   return "crypt";
	case DM_TARGET_STRIPED: return "striped";
	case DM_TARGET_ZERO:    return "zero";
	case DM_TARGET_ERROR:   return "error";
	case DM_TARGET_VERITY:  return "verity";
	default:                return "dm";
	}
}

int main(int argc, char *argv[])
{
	int show_fs_details = 0;
	int i, d, m, ctrl_fd, sda_fd;
	struct dm_ioctl_req dm_list[DM_MAX_DEVICES];
	int dm_valid[DM_MAX_DEVICES];
	int dm_parent_shown[DM_MAX_DEVICES];

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-f") == 0)
			show_fs_details = 1;
	}

	load_mounts();

	memset(dm_valid, 0, sizeof(dm_valid));
	memset(dm_parent_shown, 0, sizeof(dm_parent_shown));
	ctrl_fd = open("/dev/mapper/control", O_RDWR);
	if (ctrl_fd >= 0) {
		for (m = 0; m < DM_MAX_DEVICES; m++) {
			memset(&dm_list[m], 0, sizeof(struct dm_ioctl_req));
			dm_list[m].minor = m;
			if (ioctl(ctrl_fd, DM_IOC_STATUS, &dm_list[m]) == 0 &&
			    dm_list[m].active) {
				dm_valid[m] = 1;
			}
		}
		close(ctrl_fd);
	}

	if (show_fs_details) {
		printf("%-14s %-7s %2s %5s %2s %-7s %-6s %-10s %s\n",
		       "NAME", "MAJ:MIN", "RM", "SIZE", "RO", "TYPE", "FSTYPE", "LABEL", "MOUNTPOINT");
	} else {
		printf("%-14s %-7s %2s %5s %2s %-7s %-6s %s\n",
		       "NAME", "MAJ:MIN", "RM", "SIZE", "RO", "TYPE", "FSTYPE", "MOUNTPOINT");
	}

	for (d = 0; d < 4; d++) {
		char dev_path[16], name[8], sz_str[16], majmin[16];
		char fstype[16], label[16];
		const char *mnt;
		unsigned long sectors = 0;
		long ro = 0;
		int fd, child_cnt = 0, child_idx = 0;
		unsigned short hd_rdev = (unsigned short)((3 << 8) | (d << 6));

		sprintf(name, "hd%c", 'a' + d);
		sprintf(dev_path, "/dev/%s", name);

		fd = open(dev_path, O_RDONLY);
		if (fd < 0)
			continue;
		if (ioctl(fd, BLKGETSIZE, &sectors) < 0 || sectors == 0) {
			close(fd);
			continue;
		}
		ioctl(fd, BLKROGET, &ro);
		close(fd);

		/* Count DM children backed by this disk */
		for (m = 0; m < DM_MAX_DEVICES; m++) {
			if (dm_valid[m] && dm_list[m].num_targets > 0 &&
			    dm_list[m].targets[0].bdev == hd_rdev) {
				child_cnt++;
			}
		}

		format_size(sectors, sz_str);
		sprintf(majmin, "3:%d", d << 6);
		if (child_cnt > 0) {
			strcpy(fstype, "");
			strcpy(label, "");
		} else {
			probe_fs(dev_path, fstype, label);
		}
		mnt = find_mountpoint(dev_path, NULL, d == 0);

		if (show_fs_details) {
			printf("%-14s %-7s %2d %5s %2ld %-7s %-6s %-10s %s\n",
			       name, majmin, 0, sz_str, ro ? 1L : 0L, "disk",
			       fstype[0] ? fstype : "-",
			       label[0] ? label : "-",
			       mnt);
		} else {
			printf("%-14s %-7s %2d %5s %2ld %-7s %-6s %s\n",
			       name, majmin, 0, sz_str, ro ? 1L : 0L, "disk",
			       fstype[0] ? fstype : "-",
			       mnt);
		}

		for (m = 0; m < DM_MAX_DEVICES; m++) {
			char tree_name[32], dm_dev[32], mapper_dev[64];
			const char *dm_mnt, *t_str;

			if (!dm_valid[m] || dm_list[m].num_targets == 0 ||
			    dm_list[m].targets[0].bdev != hd_rdev)
				continue;

			child_idx++;
			dm_parent_shown[m] = 1;
			sprintf(tree_name, "%s-%s",
				(child_idx == child_cnt) ? "`" : "|",
				dm_list[m].name);
			sprintf(majmin, "%d:%d", DM_MAJOR, m);
			format_size(dm_list[m].total_sectors, sz_str);
			sprintf(dm_dev, "/dev/dm-%d", m);
			sprintf(mapper_dev, "/dev/mapper/%s", dm_list[m].name);
			probe_fs(dm_dev, fstype, label);
			dm_mnt = find_mountpoint(mapper_dev, dm_dev, 0);
			t_str = dm_type_str(dm_list[m].targets[0].type);

			if (show_fs_details) {
				printf("%-14s %-7s %2d %5s %2d %-7s %-6s %-10s %s\n",
				       tree_name, majmin, 0, sz_str, dm_list[m].ro, t_str,
				       fstype[0] ? fstype : "-",
				       label[0] ? label : "-",
				       dm_mnt);
			} else {
				printf("%-14s %-7s %2d %5s %2d %-7s %-6s %s\n",
				       tree_name, majmin, 0, sz_str, dm_list[m].ro, t_str,
				       fstype[0] ? fstype : "-",
				       dm_mnt);
			}
		}
	}

	/* Hotpluggable USB SCSI Mass Storage (/dev/sda, /dev/sda1, major 8) */
	sda_fd = open("/dev/sda", O_RDONLY);
	if (sda_fd >= 0) {
		unsigned long sda_sectors = 0;
		long sda_ro = 0;
		if (ioctl(sda_fd, BLKGETSIZE, &sda_sectors) == 0 && sda_sectors > 0) {
			char sz_str[16], fstype[16], label[16];
			const char *sda_mnt, *sda1_mnt;
			unsigned short sda_rdev = (unsigned short)((8 << 8) | 0);
			unsigned short sda1_rdev = (unsigned short)((8 << 8) | 1);
			int dm_cnt = 0, dm_idx = 0;

			ioctl(sda_fd, BLKROGET, &sda_ro);
			close(sda_fd);
			sda_fd = -1;

			for (m = 0; m < DM_MAX_DEVICES; m++) {
				if (dm_valid[m] && dm_list[m].num_targets > 0 &&
				    (dm_list[m].targets[0].bdev == sda1_rdev ||
				     dm_list[m].targets[0].bdev == sda_rdev)) {
					dm_cnt++;
				}
			}

			format_size(sda_sectors, sz_str);
			sda_mnt = find_mountpoint("/dev/sda", NULL, 0);
			if (show_fs_details) {
				printf("%-14s %-7s %2d %5s %2ld %-7s %-6s %-10s %s\n",
				       "sda", "8:0", 1, sz_str, sda_ro ? 1L : 0L, "disk",
				       "-", "-", sda_mnt);
			} else {
				printf("%-14s %-7s %2d %5s %2ld %-7s %-6s %s\n",
				       "sda", "8:0", 1, sz_str, sda_ro ? 1L : 0L, "disk",
				       "-", sda_mnt);
			}

			if (dm_cnt > 0) {
				strcpy(fstype, "crypt");
				strcpy(label, "");
			} else {
				probe_fs("/dev/sda1", fstype, label);
			}
			sda1_mnt = find_mountpoint("/dev/sda1", NULL, 0);
			if (show_fs_details) {
				printf("%-14s %-7s %2d %5s %2ld %-7s %-6s %-10s %s\n",
				       "`-sda1", "8:1", 1, sz_str, sda_ro ? 1L : 0L, "part",
				       fstype[0] ? fstype : "-",
				       label[0] ? label : "-",
				       sda1_mnt);
			} else {
				printf("%-14s %-7s %2d %5s %2ld %-7s %-6s %s\n",
				       "`-sda1", "8:1", 1, sz_str, sda_ro ? 1L : 0L, "part",
				       fstype[0] ? fstype : "-",
				       sda1_mnt);
			}

			for (m = 0; m < DM_MAX_DEVICES; m++) {
				char tree_name[32], majmin[16], dm_dev[32], mapper_dev[64];
				const char *dm_mnt, *t_str;

				if (!dm_valid[m] || dm_list[m].num_targets == 0 ||
				    (dm_list[m].targets[0].bdev != sda1_rdev &&
				     dm_list[m].targets[0].bdev != sda_rdev))
					continue;

				dm_idx++;
				dm_parent_shown[m] = 1;
				sprintf(tree_name, "  %s-%s",
					(dm_idx == dm_cnt) ? "`" : "|",
					dm_list[m].name);
				sprintf(majmin, "%d:%d", DM_MAJOR, m);
				format_size(dm_list[m].total_sectors, sz_str);
				sprintf(dm_dev, "/dev/dm-%d", m);
				sprintf(mapper_dev, "/dev/mapper/%s", dm_list[m].name);
				probe_fs(dm_dev, fstype, label);
				dm_mnt = find_mountpoint(mapper_dev, dm_dev, 0);
				t_str = dm_type_str(dm_list[m].targets[0].type);

				if (show_fs_details) {
					printf("%-14s %-7s %2d %5s %2d %-7s %-6s %-10s %s\n",
					       tree_name, majmin, 1, sz_str, dm_list[m].ro, t_str,
					       fstype[0] ? fstype : "-",
					       label[0] ? label : "-",
					       dm_mnt);
				} else {
					printf("%-14s %-7s %2d %5s %2d %-7s %-6s %s\n",
					       tree_name, majmin, 1, sz_str, dm_list[m].ro, t_str,
					       fstype[0] ? fstype : "-",
					       dm_mnt);
				}
			}
		}
		if (sda_fd >= 0)
			close(sda_fd);
	}

	/* Also list any standalone DM targets (e.g., zero / error) */
	for (m = 0; m < DM_MAX_DEVICES; m++) {
		char majmin[16], sz_str[16], dm_dev[32], mapper_dev[64];
		char fstype[16], label[16];
		const char *dm_mnt, *t_str;

		if (!dm_valid[m] || dm_parent_shown[m])
			continue;
		sprintf(majmin, "%d:%d", DM_MAJOR, m);
		format_size(dm_list[m].total_sectors, sz_str);
		sprintf(dm_dev, "/dev/dm-%d", m);
		sprintf(mapper_dev, "/dev/mapper/%s", dm_list[m].name);
		probe_fs(dm_dev, fstype, label);
		dm_mnt = find_mountpoint(mapper_dev, dm_dev, 0);
		t_str = dm_list[m].num_targets > 0 ? dm_type_str(dm_list[m].targets[0].type) : "dm";

		if (show_fs_details) {
			printf("%-14s %-7s %2d %5s %2d %-7s %-6s %-10s %s\n",
			       dm_list[m].name, majmin, 0, sz_str, dm_list[m].ro, t_str,
			       fstype[0] ? fstype : "-",
			       label[0] ? label : "-",
			       dm_mnt);
		} else {
			printf("%-14s %-7s %2d %5s %2d %-7s %-6s %s\n",
			       dm_list[m].name, majmin, 0, sz_str, dm_list[m].ro, t_str,
			       fstype[0] ? fstype : "-",
			       dm_mnt);
		}
	}

	return 0;
}
