/*
 * mv.c - Move / rename files for SIX (/bin/mv)
 */

#include <stdio.h>
#include <stdlib.h>
#include <linux/string.h>
#include <linux/unistd.h>
#include <linux/fcntl.h>
#include <stat.h>

#define BUF_SIZE 4096

static int copy_and_unlink(const char *src, const char *dst)
{
	int sfd, dfd;
	char buf[BUF_SIZE];
	int n, w;
	struct stat st;
	int mode = 0644;

	if (stat((char *)src, &st) == 0)
		mode = st.st_mode & 0777;

	sfd = open((char *)src, O_RDONLY, 0);
	if (sfd < 0) {
		perror(src);
		return 1;
	}

	dfd = open((char *)dst, O_WRONLY | O_CREAT | O_TRUNC, mode);
	if (dfd < 0) {
		perror(dst);
		close(sfd);
		return 1;
	}

	while ((n = read(sfd, buf, sizeof(buf))) > 0) {
		w = write(dfd, buf, n);
		if (w != n) {
			perror("write");
			close(sfd);
			close(dfd);
			return 1;
		}
	}

	close(sfd);
	close(dfd);
	unlink((char *)src);
	return 0;
}

static int move_file(const char *src, const char *dst)
{
	if (rename((char *)src, (char *)dst) == 0)
		return 0;
	return copy_and_unlink(src, dst);
}

static int is_dir(const char *path)
{
	struct stat st;
	if (stat((char *)path, &st) == 0)
		return S_ISDIR(st.st_mode);
	return 0;
}

int main(int argc, char **argv)
{
	char dest_buf[256];
	int i, rc = 0;

	if (argc < 3) {
		fprintf(stderr, "Usage: mv <source> <dest>\n       mv <source...> <directory>\n");
		return 1;
	}

	if (argc == 3) {
		const char *dst = argv[2];
		if (is_dir(dst)) {
			const char *base = strrchr(argv[1], '/');
			base = base ? base + 1 : argv[1];
			sprintf(dest_buf, "%s/%s", dst, base);
			return move_file(argv[1], dest_buf);
		}
		return move_file(argv[1], dst);
	}

	if (!is_dir(argv[argc - 1])) {
		fprintf(stderr, "mv: target '%s' is not a directory\n", argv[argc - 1]);
		return 1;
	}

	for (i = 1; i < argc - 1; i++) {
		const char *base = strrchr(argv[i], '/');
		base = base ? base + 1 : argv[i];
		sprintf(dest_buf, "%s/%s", argv[argc - 1], base);
		if (move_file(argv[i], dest_buf) != 0)
			rc = 1;
	}

	return rc;
}
