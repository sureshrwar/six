/*
 * dmsetup - Device Mapper administration utility for SIX
 *
 * Usage:
 *   dmsetup create <name> --table "<start> <length> <target> [args...]"
 *   dmsetup remove <name>
 *   dmsetup remove_all
 *   dmsetup suspend <name>
 *   dmsetup resume <name>
 *   dmsetup ls
 *   dmsetup status [<name>]
 *   dmsetup table [--showkeys] [<name>]
 *   dmsetup info [<name>]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include "../../include/linux/dm.h"

static int open_control_fd(void)
{
	int fd = open("/dev/mapper/control", O_RDWR);
	if (fd < 0)
		fd = open("/dev/dm-0", O_RDWR);
	return fd;
}

static unsigned short resolve_bdev(const char *path)
{
	struct stat st;
	int maj = 0, min = 0;

	if (strcmp(path, "/dev/hda") == 0)
		return (3 << 8) | 0;
	if (strcmp(path, "/dev/hdb") == 0)
		return (3 << 8) | 64;
	if (strcmp(path, "/dev/hdc") == 0)
		return (3 << 8) | 128;
	if (strcmp(path, "/dev/hdd") == 0)
		return (3 << 8) | 192;

	if (strchr(path, ':')) {
		maj = atoi(path);
		min = atoi(strchr(path, ':') + 1);
		return (unsigned short)((maj << 8) | (min & 0xff));
	}

	if (stat(path, &st) == 0 && st.st_rdev)
		return (unsigned short)st.st_rdev;

	return (3 << 8) | 128; /* default to /dev/hdc */
}

static int split_tokens(char *line, char *toks[], int max_toks)
{
	int n = 0;
	char *p = line;

	while (*p && n < max_toks) {
		while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
			p++;
		if (!*p)
			break;
		toks[n++] = p;
		while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
			p++;
		if (*p)
			*p++ = '\0';
	}
	return n;
}

static int parse_table_line(const char *table_str, struct dm_target_spec *t)
{
	char buf[256];
	char *toks[12];
	int n;

	memset(t, 0, sizeof(*t));
	strncpy(buf, table_str, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	n = split_tokens(buf, toks, 12);
	if (n < 3)
		return -1;

	t->start_sector = (unsigned long)atol(toks[0]);
	t->num_sectors = (unsigned long)atol(toks[1]);

	if (strcmp(toks[2], "linear") == 0) {
		if (n < 5)
			return -1;
		t->type = DM_TARGET_LINEAR;
		strncpy(t->dev_name, toks[3], sizeof(t->dev_name) - 1);
		t->bdev = resolve_bdev(toks[3]);
		t->offset_sector = (unsigned long)atol(toks[4]);
		return 0;
	} else if (strcmp(toks[2], "crypt") == 0) {
		t->type = DM_TARGET_CRYPT;
		if (n >= 8) {
			/* <start> <len> crypt <cipher> <key> <iv_off> <dev> <off> */
			strncpy(t->cipher, toks[3], sizeof(t->cipher) - 1);
			strncpy(t->key, toks[4], sizeof(t->key) - 1);
			t->iv_offset = (unsigned long)atol(toks[5]);
			strncpy(t->dev_name, toks[6], sizeof(t->dev_name) - 1);
			t->bdev = resolve_bdev(toks[6]);
			t->offset_sector = (unsigned long)atol(toks[7]);
			return 0;
		} else if (n >= 6) {
			/* shorthand: <start> <len> crypt <key> <dev> <off> */
			strcpy(t->cipher, "chacha20");
			strncpy(t->key, toks[3], sizeof(t->key) - 1);
			t->iv_offset = 0;
			strncpy(t->dev_name, toks[4], sizeof(t->dev_name) - 1);
			t->bdev = resolve_bdev(toks[4]);
			t->offset_sector = (unsigned long)atol(toks[5]);
			return 0;
		}
		return -1;
	} else if (strcmp(toks[2], "striped") == 0) {
		/* <start> <len> striped 2 <chunk_sectors> <dev1> <off1> <dev2> <off2> */
		if (n < 9)
			return -1;
		t->type = DM_TARGET_STRIPED;
		t->chunk_sectors = (unsigned long)atol(toks[4]);
		strncpy(t->dev_name, toks[5], sizeof(t->dev_name) - 1);
		t->bdev = resolve_bdev(toks[5]);
		t->offset_sector = (unsigned long)atol(toks[6]);
		strncpy(t->dev_name2, toks[7], sizeof(t->dev_name2) - 1);
		t->bdev2 = resolve_bdev(toks[7]);
		t->offset_sector2 = (unsigned long)atol(toks[8]);
		return 0;
	} else if (strcmp(toks[2], "zero") == 0) {
		t->type = DM_TARGET_ZERO;
		return 0;
	} else if (strcmp(toks[2], "error") == 0) {
		t->type = DM_TARGET_ERROR;
		return 0;
	}
	return -1;
}

static const char *target_type_str(int type)
{
	switch (type) {
	case DM_TARGET_LINEAR:  return "linear";
	case DM_TARGET_CRYPT:   return "crypt";
	case DM_TARGET_STRIPED: return "striped";
	case DM_TARGET_ZERO:    return "zero";
	case DM_TARGET_ERROR:   return "error";
	default:                return "unknown";
	}
}

static void print_table_entry(struct dm_target_spec *t, int show_keys)
{
	if (t->type == DM_TARGET_LINEAR) {
		printf("%lu %lu linear %s %lu\n",
		       t->start_sector, t->num_sectors,
		       t->dev_name[0] ? t->dev_name : "/dev/hdc",
		       t->offset_sector);
	} else if (t->type == DM_TARGET_CRYPT) {
		printf("%lu %lu crypt %s %s %lu %s %lu\n",
		       t->start_sector, t->num_sectors,
		       t->cipher[0] ? t->cipher : "chacha20",
		       show_keys ? t->key : "0000000000000000",
		       t->iv_offset,
		       t->dev_name[0] ? t->dev_name : "/dev/hdc",
		       t->offset_sector);
	} else if (t->type == DM_TARGET_STRIPED) {
		printf("%lu %lu striped 2 %lu %s %lu %s %lu\n",
		       t->start_sector, t->num_sectors,
		       t->chunk_sectors,
		       t->dev_name[0] ? t->dev_name : "/dev/hdc",
		       t->offset_sector,
		       t->dev_name2[0] ? t->dev_name2 : "/dev/hdc",
		       t->offset_sector2);
	} else {
		printf("%lu %lu %s\n",
		       t->start_sector, t->num_sectors,
		       target_type_str(t->type));
	}
}

static void usage(void)
{
	printf("Usage:\n"
	       "  dmsetup create <name> [--ro] [--minor N] --table \"<start> <len> <target> ...\"\n"
	       "  dmsetup remove <name>\n"
	       "  dmsetup remove_all\n"
	       "  dmsetup suspend <name>\n"
	       "  dmsetup resume <name>\n"
	       "  dmsetup ls\n"
	       "  dmsetup status [<name>]\n"
	       "  dmsetup table [--showkeys] [<name>]\n"
	       "  dmsetup info [<name>]\n\n"
	       "Supported targets:\n"
	       "  linear  <dev> <start_sector>\n"
	       "  crypt   <cipher> <key> <iv_offset> <dev> <start_sector>\n"
	       "  striped 2 <chunk_sectors> <dev1> <off1> <dev2> <off2>\n"
	       "  zero\n"
	       "  error\n");
}

int main(int argc, char *argv[])
{
	int fd, i, j;
	struct dm_ioctl_req req;

	if (argc < 2) {
		usage();
		return 1;
	}

	fd = open_control_fd();
	if (fd < 0) {
		printf("dmsetup: cannot open /dev/mapper/control\n");
		return 1;
	}

	if (strcmp(argv[1], "create") == 0) {
		char mapper_path[64], dm_path[32];
		if (argc < 3) {
			usage();
			close(fd);
			return 1;
		}
		memset(&req, 0, sizeof(req));
		req.minor = -1;
		strncpy(req.name, argv[2], DM_NAME_LEN - 1);

		for (i = 3; i < argc; i++) {
			if (strcmp(argv[i], "--ro") == 0 || strcmp(argv[i], "-r") == 0) {
				req.ro = 1;
			} else if (strcmp(argv[i], "--minor") == 0 && i + 1 < argc) {
				req.minor = atoi(argv[++i]);
			} else if (strcmp(argv[i], "--table") == 0 && i + 1 < argc) {
				if (req.num_targets >= DM_MAX_TARGETS) {
					printf("dmsetup: too many targets\n");
					close(fd);
					return 1;
				}
				if (parse_table_line(argv[++i], &req.targets[req.num_targets]) < 0) {
					printf("dmsetup: invalid table specification: %s\n", argv[i]);
					close(fd);
					return 1;
				}
				req.num_targets++;
			}
		}

		if (req.num_targets == 0) {
			char line[256];
			while (fgets(line, sizeof(line), stdin)) {
				if (line[0] == '\n' || line[0] == '#')
					continue;
				if (req.num_targets >= DM_MAX_TARGETS)
					break;
				if (parse_table_line(line, &req.targets[req.num_targets]) == 0)
					req.num_targets++;
			}
		}

		if (req.num_targets == 0) {
			printf("dmsetup: no target table specified\n");
			close(fd);
			return 1;
		}

		if (ioctl(fd, DM_IOC_CREATE, &req) < 0) {
			printf("dmsetup: failed to create %s\n", req.name);
			close(fd);
			return 1;
		}

		mkdir("/dev/mapper", 0755);
		sprintf(mapper_path, "/dev/mapper/%s", req.name);
		sprintf(dm_path, "/dev/dm-%d", req.minor);
		unlink(mapper_path);
		symlink(dm_path, mapper_path);
		printf("Created %s -> %s (%lu sectors, %lu KB)\n",
		       mapper_path, dm_path, req.total_sectors, req.total_sectors >> 1);
		close(fd);
		return 0;
	}

	if (strcmp(argv[1], "remove") == 0) {
		char mapper_path[64];
		if (argc < 3) {
			usage();
			close(fd);
			return 1;
		}
		memset(&req, 0, sizeof(req));
		req.minor = -1;
		strncpy(req.name, argv[2], DM_NAME_LEN - 1);
		if (ioctl(fd, DM_IOC_REMOVE, &req) < 0) {
			printf("dmsetup: failed to remove %s\n", argv[2]);
			close(fd);
			return 1;
		}
		sprintf(mapper_path, "/dev/mapper/%s", argv[2]);
		unlink(mapper_path);
		printf("Removed %s\n", mapper_path);
		close(fd);
		return 0;
	}

	if (strcmp(argv[1], "remove_all") == 0) {
		for (i = 0; i < DM_MAX_DEVICES; i++) {
			memset(&req, 0, sizeof(req));
			req.minor = i;
			if (ioctl(fd, DM_IOC_STATUS, &req) == 0 && req.active) {
				char mapper_path[64];
				sprintf(mapper_path, "/dev/mapper/%s", req.name);
				unlink(mapper_path);
			}
		}
		ioctl(fd, DM_IOC_REMOVE_ALL, 0);
		close(fd);
		return 0;
	}

	if (strcmp(argv[1], "suspend") == 0 || strcmp(argv[1], "resume") == 0) {
		int cmd = (strcmp(argv[1], "suspend") == 0) ? DM_IOC_SUSPEND : DM_IOC_RESUME;
		if (argc < 3) {
			usage();
			close(fd);
			return 1;
		}
		memset(&req, 0, sizeof(req));
		req.minor = -1;
		strncpy(req.name, argv[2], DM_NAME_LEN - 1);
		if (ioctl(fd, cmd, &req) < 0) {
			printf("dmsetup: %s failed for %s\n", argv[1], argv[2]);
			close(fd);
			return 1;
		}
		close(fd);
		return 0;
	}

	if (strcmp(argv[1], "ls") == 0) {
		int count = 0;
		for (i = 0; i < DM_MAX_DEVICES; i++) {
			memset(&req, 0, sizeof(req));
			req.minor = i;
			if (ioctl(fd, DM_IOC_STATUS, &req) == 0 && req.active) {
				printf("%-16s (%d, %d)\n", req.name, DM_MAJOR, req.minor);
				count++;
			}
		}
		if (count == 0)
			printf("No devices found\n");
		close(fd);
		return 0;
	}

	if (strcmp(argv[1], "status") == 0 || strcmp(argv[1], "table") == 0 ||
	    strcmp(argv[1], "info") == 0) {
		int is_table = (strcmp(argv[1], "table") == 0);
		int is_info = (strcmp(argv[1], "info") == 0);
		int show_keys = 0;
		const char *filter_name = NULL;

		for (i = 2; i < argc; i++) {
			if (strcmp(argv[i], "--showkeys") == 0)
				show_keys = 1;
			else
				filter_name = argv[i];
		}

		for (i = 0; i < DM_MAX_DEVICES; i++) {
			memset(&req, 0, sizeof(req));
			req.minor = i;
			if (ioctl(fd, DM_IOC_STATUS, &req) < 0 || !req.active)
				continue;
			if (filter_name && strcmp(req.name, filter_name) != 0)
				continue;

			if (is_info) {
				printf("Name:              %s\n"
				       "State:             %s\n"
				       "Read Ahead:        8\n"
				       "Tables present:    LIVE\n"
				       "Segments:          %d\n"
				       "Size:              %lu sectors (%lu KB)\n"
				       "Major, minor:      %d, %d (/dev/dm-%d)\n"
				       "I/O stats:         %lu reads (%lu sectors), %lu writes (%lu sectors)\n\n",
				       req.name,
				       req.suspended ? "SUSPENDED" : (req.ro ? "ACTIVE (READ-ONLY)" : "ACTIVE"),
				       req.num_targets,
				       req.total_sectors, req.total_sectors >> 1,
				       DM_MAJOR, req.minor, req.minor,
				       req.read_ios, req.read_sectors,
				       req.write_ios, req.write_sectors);
			} else if (is_table) {
				for (j = 0; j < req.num_targets; j++) {
					if (!filter_name)
						printf("%s: ", req.name);
					print_table_entry(&req.targets[j], show_keys);
				}
			} else {
				for (j = 0; j < req.num_targets; j++) {
					printf("%s: %lu %lu %s (reads=%lu, writes=%lu)\n",
					       req.name,
					       req.targets[j].start_sector,
					       req.targets[j].num_sectors,
					       target_type_str(req.targets[j].type),
					       req.read_ios, req.write_ios);
				}
			}
		}
		close(fd);
		return 0;
	}

	usage();
	close(fd);
	return 1;
}
