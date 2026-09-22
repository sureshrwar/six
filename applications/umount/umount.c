/*
 * umount.c - Detach a filesystem for SIX (/bin/umount)
 *
 * This matters more here than it might elsewhere.  halt(8) is
 * kill(1, SIGTERM) followed by reboot(), and sys_reboot() under SIX goes
 * straight to do_exit() without calling sys_sync() -- see kernel/sys.c.
 * Nothing unmounts anything on the way down.
 *
 * For the root filesystem the established discipline is to run sync
 * before halt.  That flushes the buffers but still leaves the superblock
 * marked dirty, which is why an auxiliary filesystem wants a real umount:
 * it drops s_dirt and clears the valid flag, so e2fsck on the host does
 * not report "not cleanly unmounted" after every session.
 *
 * Note this is umount(2), not umount2(2): one argument, and the kernel
 * accepts either the mount point or the device name.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <linux/fcntl.h>
#include <linux/unistd.h>
#include <sys/mount.h>

static int umount_all(void)
{
	char buf[4096];
	char *mnts[32];
	int fd, n, count = 0, i, ret = 0;
	char *p;

	fd = open("/proc/mounts", O_RDONLY);
	if (fd < 0) {
		perror("/proc/mounts");
		return 1;
	}
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return 0;

	buf[n] = '\0';
	p = buf;
	while (*p && count < 32) {
		char *line = p;
		char *mp;

		while (*p && *p != '\n')
			p++;
		if (*p == '\n')
			*p++ = '\0';

		while (*line == ' ' || *line == '\t')
			line++;
		while (*line && *line != ' ' && *line != '\t')
			line++;
		while (*line == ' ' || *line == '\t')
			line++;
		if (!*line)
			continue;

		mp = line;
		while (*line && *line != ' ' && *line != '\t')
			line++;
		*line = '\0';
		if (strcmp(mp, "/") != 0 && strcmp(mp, "/proc") != 0)
			mnts[count++] = mp;
	}

	for (i = count - 1; i >= 0; i--) {
		if (umount(mnts[i]) < 0) {
			perror(mnts[i]);
			ret = 1;
		}
	}
	return ret;
}

int main(int argc, char **argv)
{
	int i, ret = 0;

	if (argc < 2) {
		fprintf(stderr, "Usage: umount [-a | <dir|device...>]\n");
		return 1;
	}

	if (strcmp(argv[1], "-a") == 0)
		return umount_all();

	for (i = 1; i < argc; i++) {
		int rc = umount(argv[i]);

		if (rc < 0) {
			perror(argv[i]);
			ret = 1;
		}
	}

	return ret;
}
