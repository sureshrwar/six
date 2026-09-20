/*
 * tar.c - Tape archiver for SIX
 *
 * POSIX ustar compatible archive creation, listing, and extraction.
 *
 * Usage:
 *   tar {c|x|t}[v][f archive] [-C dir] [files/dirs...]
 *   tar -c|-x|-t [-v] [-f archive] [-C dir] [files/dirs...]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>

#define BLOCK_SIZE 512

struct tar_header {
	char name[100];
	char mode[8];
	char uid[8];
	char gid[8];
	char size[12];
	char mtime[12];
	char chksum[8];
	char typeflag;
	char linkname[100];
	char magic[6];
	char version[2];
	char uname[32];
	char gname[32];
	char devmajor[8];
	char devminor[8];
	char prefix[155];
	char padding[12];
};

static int opt_create = 0;
static int opt_extract = 0;
static int opt_list = 0;
static int opt_verbose = 0;
static const char *opt_file = NULL;
static const char *opt_cd = NULL;

static void set_octal(char *dst, size_t len, unsigned long val)
{
	char fmt[16];
	snprintf(fmt, sizeof(fmt), "%%0%ldlo", (long)(len - 1));
	snprintf(dst, len, fmt, val);
	dst[len - 1] = ' ';
}

static unsigned long parse_octal(const char *src, size_t len)
{
	unsigned long val = 0;
	while (len > 0 && (*src == ' ' || *src == '\0')) {
		src++;
		len--;
	}
	while (len > 0 && *src >= '0' && *src <= '7') {
		val = (val << 3) | (*src - '0');
		src++;
		len--;
	}
	return val;
}

static unsigned int calculate_checksum(const struct tar_header *h)
{
	const unsigned char *p = (const unsigned char *)h;
	unsigned int sum = 0;
	size_t i;

	for (i = 0; i < sizeof(struct tar_header); i++) {
		if (i >= 148 && i < 156) {
			sum += ' '; /* Checksum field treated as spaces */
		} else {
			sum += p[i];
		}
	}
	return sum;
}

static void ensure_parent_dirs(char *path)
{
	char *p = path;
	if (*p == '/') p++;
	while ((p = strchr(p, '/')) != NULL) {
		*p = '\0';
		mkdir(path, 0755);
		*p = '/';
		p++;
	}
}

static void write_null_blocks(int fd, int count)
{
	char zero[BLOCK_SIZE];
	memset(zero, 0, sizeof(zero));
	int i;
	for (i = 0; i < count; i++) {
		write(fd, zero, BLOCK_SIZE);
	}
}

static int archive_file(int out_fd, const char *path, const char *rel_name)
{
	struct stat st;
	if (lstat(path, &st) != 0) {
		fprintf(stderr, "tar: cannot stat %s\n", path);
		return -1;
	}

	struct tar_header h;
	memset(&h, 0, sizeof(h));

	char full_name[256];
	strncpy(full_name, rel_name, sizeof(full_name) - 1);
	full_name[sizeof(full_name) - 1] = '\0';

	if (S_ISDIR(st.st_mode)) {
		size_t len = strlen(full_name);
		if (len > 0 && full_name[len - 1] != '/' && len + 1 < sizeof(full_name)) {
			full_name[len] = '/';
			full_name[len + 1] = '\0';
		}
		h.typeflag = '5';
		set_octal(h.size, sizeof(h.size), 0);
	} else if (S_ISREG(st.st_mode)) {
		h.typeflag = '0';
		set_octal(h.size, sizeof(h.size), st.st_size);
	} else {
		/* Skip non-regular non-dir files for simple archiving */
		return 0;
	}

	strncpy(h.name, full_name, sizeof(h.name) - 1);
	set_octal(h.mode, sizeof(h.mode), st.st_mode & 07777);
	set_octal(h.uid, sizeof(h.uid), st.st_uid);
	set_octal(h.gid, sizeof(h.gid), st.st_gid);
	set_octal(h.mtime, sizeof(h.mtime), st.st_mtime);

	memcpy(h.magic, "ustar  ", 6);
	memcpy(h.uname, "root", 4);
	memcpy(h.gname, "root", 4);

	unsigned int chksum = calculate_checksum(&h);
	snprintf(h.chksum, sizeof(h.chksum), "%06o", chksum);
	h.chksum[6] = '\0';
	h.chksum[7] = ' ';

	write(out_fd, &h, sizeof(h));
	if (opt_verbose) {
		printf("%s\n", full_name);
	}

	if (S_ISREG(st.st_mode) && st.st_size > 0) {
		int in_fd = open(path, O_RDONLY);
		if (in_fd < 0) {
			fprintf(stderr, "tar: cannot open %s\n", path);
			return -1;
		}
		char buf[BLOCK_SIZE];
		ssize_t n;
		off_t written = 0;
		while ((n = read(in_fd, buf, sizeof(buf))) > 0) {
			if (n < BLOCK_SIZE) {
				memset(buf + n, 0, BLOCK_SIZE - n);
			}
			write(out_fd, buf, BLOCK_SIZE);
			written += n;
		}
		close(in_fd);
	} else if (S_ISDIR(st.st_mode)) {
		DIR *dir = opendir(path);
		if (dir) {
			struct dirent *ent;
			while ((ent = readdir(dir)) != NULL) {
				if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
					continue;
				char subpath[1024];
				char subrel[1024];
				snprintf(subpath, sizeof(subpath), "%s/%s", path, ent->d_name);
				snprintf(subrel, sizeof(subrel), "%s/%s", rel_name, ent->d_name);
				archive_file(out_fd, subpath, subrel);
			}
			closedir(dir);
		}
	}
	return 0;
}

static void do_create(int argc, char **argv, int start_idx)
{
	int out_fd = 1; /* stdout */
	if (opt_file && strcmp(opt_file, "-") != 0) {
		out_fd = open(opt_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (out_fd < 0) {
			fprintf(stderr, "tar: cannot create %s\n", opt_file);
			exit(1);
		}
	}

	if (opt_cd) {
		if (chdir(opt_cd) != 0) {
			fprintf(stderr, "tar: cannot chdir to %s\n", opt_cd);
			exit(1);
		}
	}

	int i;
	for (i = start_idx; i < argc; i++) {
		const char *target = argv[i];
		archive_file(out_fd, target, target);
	}

	write_null_blocks(out_fd, 2);

	if (out_fd != 1) {
		close(out_fd);
	}
}

static void do_list_or_extract(int extract)
{
	int in_fd = 0; /* stdin */
	if (opt_file && strcmp(opt_file, "-") != 0) {
		in_fd = open(opt_file, O_RDONLY);
		if (in_fd < 0) {
			fprintf(stderr, "tar: cannot open %s\n", opt_file);
			exit(1);
		}
	}

	if (opt_cd) {
		if (chdir(opt_cd) != 0) {
			fprintf(stderr, "tar: cannot chdir to %s\n", opt_cd);
			exit(1);
		}
	}

	struct tar_header h;
	int empty_count = 0;

	while (1) {
		ssize_t n = read(in_fd, &h, sizeof(h));
		if (n <= 0) break;
		if (n < (ssize_t)sizeof(h)) {
			fprintf(stderr, "tar: unexpected EOF in archive\n");
			break;
		}

		/* Check for empty block */
		if (h.name[0] == '\0') {
			empty_count++;
			if (empty_count >= 2) break;
			continue;
		}
		empty_count = 0;

		unsigned int expected_sum = calculate_checksum(&h);
		unsigned int actual_sum = parse_octal(h.chksum, sizeof(h.chksum));
		if (expected_sum != actual_sum) {
			fprintf(stderr, "tar: checksum mismatch (header corrupted)\n");
			break;
		}

		unsigned long mode = parse_octal(h.mode, sizeof(h.mode));
		unsigned long size = parse_octal(h.size, sizeof(h.size));
		char name[256];
		strncpy(name, h.name, sizeof(h.name));
		name[sizeof(h.name)] = '\0';

		if (extract) {
			if (opt_verbose) {
				printf("%s\n", name);
			}
			if (h.typeflag == '5' || name[strlen(name) - 1] == '/') {
				ensure_parent_dirs(name);
				mkdir(name, mode ? (mode & 07777) : 0755);
			} else {
				ensure_parent_dirs(name);
				int out = open(name, O_WRONLY | O_CREAT | O_TRUNC, mode ? (mode & 07777) : 0644);
				if (out < 0) {
					fprintf(stderr, "tar: cannot create %s\n", name);
				}
				off_t remaining = size;
				char buf[BLOCK_SIZE];
				while (remaining > 0) {
					ssize_t r = read(in_fd, buf, BLOCK_SIZE);
					if (r <= 0) break;
					size_t write_sz = (remaining > BLOCK_SIZE) ? BLOCK_SIZE : remaining;
					if (out >= 0) {
						write(out, buf, write_sz);
					}
					remaining -= write_sz;
				}
				if (out >= 0) close(out);
			}
		} else {
			/* List */
			if (opt_verbose) {
				printf("%06lo %10lu %s\n", mode, size, name);
			} else {
				printf("%s\n", name);
			}
			/* Skip content blocks */
			off_t blocks = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
			off_t b;
			char skip[BLOCK_SIZE];
			for (b = 0; b < blocks; b++) {
				read(in_fd, skip, BLOCK_SIZE);
			}
		}
	}

	if (in_fd != 0) close(in_fd);
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: tar {c|x|t}[v][f archive] [-C dir] [files...]\n");
		return 1;
	}

	int arg_idx = 1;
	char *opts = argv[arg_idx++];
	if (opts[0] == '-') opts++;

	char *p = opts;
	while (*p) {
		if (*p == 'c') opt_create = 1;
		else if (*p == 'x') opt_extract = 1;
		else if (*p == 't') opt_list = 1;
		else if (*p == 'v') opt_verbose = 1;
		else if (*p == 'f') {
			if (arg_idx < argc) {
				opt_file = argv[arg_idx++];
			} else {
				fprintf(stderr, "tar: option 'f' requires an argument\n");
				return 1;
			}
		} else if (*p == 'C') {
			if (arg_idx < argc) {
				opt_cd = argv[arg_idx++];
			} else {
				fprintf(stderr, "tar: option 'C' requires an argument\n");
				return 1;
			}
		} else {
			fprintf(stderr, "tar: unknown option '%c'\n", *p);
			return 1;
		}
		p++;
	}

	/* Also parse any additional -f or -C arguments */
	while (arg_idx < argc && argv[arg_idx][0] == '-') {
		if (strcmp(argv[arg_idx], "-f") == 0 && arg_idx + 1 < argc) {
			opt_file = argv[++arg_idx];
			arg_idx++;
		} else if (strcmp(argv[arg_idx], "-C") == 0 && arg_idx + 1 < argc) {
			opt_cd = argv[++arg_idx];
			arg_idx++;
		} else if (strcmp(argv[arg_idx], "-v") == 0) {
			opt_verbose = 1;
			arg_idx++;
		} else {
			break;
		}
	}

	if (!opt_create && !opt_extract && !opt_list) {
		fprintf(stderr, "tar: must specify one of -c, -x, or -t\n");
		return 1;
	}

	if (opt_create) {
		do_create(argc, argv, arg_idx);
	} else if (opt_extract) {
		do_list_or_extract(1);
	} else if (opt_list) {
		do_list_or_extract(0);
	}

	return 0;
}
