/*
 * mount.c - Attach a filesystem for SIX (/bin/mount)
 *
 * SIX has never had a mount command.  The root filesystem is attached by
 * do_mount_root() in fs/super.c, which does not go anywhere near the mount
 * system call: it fabricates an inode carrying ROOT_DEV, opens that, and
 * assigns the result straight to current->fs->root.  Nothing else was ever
 * mounted, so the syscall stub in library/sys/mount.c sat unused.
 *
 * With a second disk that changes.  /dev/hdb is deliberately left
 * unmounted at boot -- where it belongs in the namespace is a policy
 * question the kernel has no business answering -- so there has to be a
 * way for userspace, whether /etc/rc or an interactive shell, to say
 * where it goes.
 *
 * Unlike mount_root(), sys_mount() really does resolve the device path, so
 * the /dev/hdb node has to exist and be a block special file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <linux/string.h>
#include <linux/unistd.h>
#include <sys/mount.h>

/*
 * From library/libc/perror.c.  Declared here rather than by including the
 * guest's <string.h>, which would collide with the <linux/string.h> above
 * that everything else in applications/ uses for strcmp and friends.
 */
extern char *strerror(int errnum);

/*
 * Filesystems to try when no -t was given, in the order the kernel itself
 * would try them.  ext4 first: fs/ext4/super.c refuses anything without
 * the EXTENTS incompat feature, so a plain ext2 image falls through to
 * fs/ext2 rather than being misread.  That ordering is what makes it
 * possible to have an ext2 /dev/hdb under an ext4 root.
 */
static const char *autotypes[] = { "ext4", "ext2", "minix", 0 };

static void usage(void)
{
	fprintf(stderr,
		"Usage: mount                          list mounted filesystems\n"
		"       mount [-t type] [-o opts] dev dir\n"
		"\n"
		"  -t type   filesystem type; if omitted, each of ext4, ext2 and\n"
		"            minix is tried in turn\n"
		"  -o opts   comma separated: ro, rw, nosuid, nodev, noexec, sync,\n"
		"            remount\n");
}

/*
 * There is no mount table to read.
 *
 * The kernel can produce one -- get_filesystem_info() in fs/super.c walks
 * the vfsmnt list that add_vfsmnt() maintains, and fs/proc/array.c serves
 * it as /proc/mounts -- but CONFIG_PROC_FS is undef in this build, so
 * fs/proc is not compiled and "proc" is not a registered filesystem.  Say
 * that plainly rather than suggesting a mount command that would only
 * come back with ENODEV.
 */
static int show_mounts(void)
{
	char buf[1024];
	FILE *f = fopen("/proc/mounts", "r");
	int n;

	if (!f) {
		fprintf(stderr,
			"mount: no mount table available -- this kernel is built\n"
			"       without CONFIG_PROC_FS, so there is no /proc/mounts.\n");
		return 1;
	}

	while ((n = fread(buf, 1, sizeof(buf) - 1, f)) > 0) {
		buf[n] = '\0';
		fputs(buf, stdout);
	}

	fclose(f);
	return 0;
}

/*
 * Parse a -o list.  Returns 0 on success, -1 on an unrecognised option.
 * Only the flags the 2.0 kernel actually honours are accepted; silently
 * ignoring the rest would be worse than refusing them, since the caller
 * would have no way to tell that "ro" had been dropped.
 */
static int parse_options(char *opts, unsigned long *flags)
{
	char *p = opts;

	while (*p) {
		char *end = strchr(p, ',');
		int len;

		if (end)
			*end = '\0';
		len = strlen(p);

		if (!len)
			;				/* empty field, e.g. "ro,," */
		else if (!strcmp(p, "ro"))
			*flags |= MS_RDONLY;
		else if (!strcmp(p, "rw"))
			*flags &= ~MS_RDONLY;
		else if (!strcmp(p, "nosuid"))
			*flags |= MS_NOSUID;
		else if (!strcmp(p, "nodev"))
			*flags |= MS_NODEV;
		else if (!strcmp(p, "noexec"))
			*flags |= MS_NOEXEC;
		else if (!strcmp(p, "sync"))
			*flags |= MS_SYNCHRONOUS;
		else if (!strcmp(p, "remount"))
			*flags |= MS_REMOUNT;
		else if (!strcmp(p, "defaults"))
			;
		else {
			fprintf(stderr, "mount: unknown option \"%s\"\n", p);
			return -1;
		}

		if (!end)
			break;
		p = end + 1;
	}

	return 0;
}

int main(int argc, char **argv)
{
	char *type = 0;
	char *dev = 0, *dir = 0;
	unsigned long flags = MS_MGC_VAL;
	int i, rc;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-t")) {
			if (++i >= argc) {
				usage();
				return 1;
			}
			type = argv[i];
		} else if (!strcmp(argv[i], "-o")) {
			if (++i >= argc) {
				usage();
				return 1;
			}
			if (parse_options(argv[i], &flags) != 0)
				return 1;
		} else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			usage();
			return 0;
		} else if (argv[i][0] == '-') {
			fprintf(stderr, "mount: unknown option \"%s\"\n", argv[i]);
			usage();
			return 1;
		} else if (!dev)
			dev = argv[i];
		else if (!dir)
			dir = argv[i];
		else {
			usage();
			return 1;
		}
	}

	if (!dev && !dir)
		return show_mounts();

	if (!dev || !dir) {
		usage();
		return 1;
	}

	if (type) {
		rc = mount(dev, dir, type, flags, 0);
	} else {
		/*
		 * Probe.  Every failed attempt looks the same from here, so
		 * report the last errno rather than the first: the earlier
		 * types are the ones we expected to fail.
		 */
		rc = -1;
		errno = EINVAL;
		for (i = 0; autotypes[i]; i++) {
			rc = mount(dev, dir, (char *)autotypes[i], flags, 0);
			if (rc == 0) {
				type = (char *)autotypes[i];
				break;
			}
		}
	}

	if (rc < 0) {
		/*
		 * Build the message in one call.  perror() writes straight to
		 * fd 2 with write(2) while fprintf() on stderr goes through
		 * the stdio buffer, so using both put the prefix on screen
		 * *after* the message it introduces.  strerror() gives the
		 * same text without the ordering problem.
		 */

		if (type)
			fprintf(stderr, "mount: %s on %s as %s: %s\n",
				dev, dir, type, strerror(errno));
		else
			fprintf(stderr, "mount: %s on %s: %s "
				"(tried ext4, ext2, minix)\n",
				dev, dir, strerror(errno));
		fflush(stderr);
		return 1;
	}

	return 0;
}
