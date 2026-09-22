#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stat.h>
#include <linux/dirent.h>

extern int getdents(unsigned int fd, struct dirent *dirp, unsigned int count);

static int opt_summary = 0;
static int opt_human = 0;

static void
print_kb(unsigned long kb, const char *path)
{
	if (opt_human) {
		if (kb >= 1024UL)
			printf("%lu.%luM\t%s\n", kb / 1024UL, ((kb % 1024UL) * 10UL) / 1024UL, path);
		else
			printf("%luK\t%s\n", kb, path);
	} else {
		printf("%lu\t%s\n", kb, path);
	}
}

static unsigned long
du_path(const char *path, int depth)
{
	struct stat st;
	unsigned long total_kb;

	if (lstat(path, &st) < 0) {
		perror(path);
		return 0;
	}

	total_kb = ((unsigned long)st.st_size + 1023UL) / 1024UL;
	if (total_kb == 0 && st.st_size > 0)
		total_kb = 1;

	if (S_ISDIR(st.st_mode)) {
		char dbuf[512];
		int fd = open(path, 0);
		if (fd >= 0) {
			int n;
			while ((n = getdents(fd, (struct dirent *)dbuf, sizeof(dbuf))) > 0) {
				int off = 0;
				while (off < n) {
					struct dirent *de = (struct dirent *)(dbuf + off);
					char child[256];
					off += de->d_reclen;
					if (de->d_ino == 0)
						continue;
					if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
						continue;
					if (strcmp(path, "/") == 0)
						snprintf(child, sizeof(child), "/%s", de->d_name);
					else
						snprintf(child, sizeof(child), "%s/%s", path, de->d_name);
					total_kb += du_path(child, depth + 1);
				}
			}
			close(fd);
		}
		if (!opt_summary || depth == 0)
			print_kb(total_kb, path);
	} else if (depth == 0) {
		print_kb(total_kb, path);
	}
	return total_kb;
}

int
main(int argc, char **argv)
{
	int i = 1;

	while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
		const char *p = argv[i] + 1;
		while (*p) {
			if (*p == 's') opt_summary = 1;
			else if (*p == 'h') opt_human = 1;
			else if (*p == 'k') ;
			p++;
		}
		i++;
	}

	if (i >= argc) {
		du_path(".", 0);
	} else {
		for (; i < argc; i++)
			du_path(argv[i], 0);
	}
	return 0;
}
