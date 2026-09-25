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
			"mount: cannot open /proc/mounts (try: mount -t proc proc /proc)\n");
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
static int parse_options(char *opts, unsigned long *flags,
			 char *fs_data, int fs_data_max)
{
	char *p = opts;
	int dlen = strlen(fs_data);

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
			/*
			 * Filesystem-specific option (e.g. FUSE's
			 * "fd=3,rootmode=040755,user_id=0,group_id=0"):
			 * append it to the data string handed to mount(2).
			 */
			if (dlen + (dlen ? 1 : 0) + len + 1 >= fs_data_max) {
				fprintf(stderr, "mount: option string too long\n");
				return -1;
			}
			if (dlen > 0)
				fs_data[dlen++] = ',';
			memcpy(fs_data + dlen, p, len);
			dlen += len;
			fs_data[dlen] = '\0';
		}

		if (!end)
			break;
		p = end + 1;
	}

	return 0;
}

#include <linux/fcntl.h>
#include <linux/wait.h>
#include <stat.h>

static int is_ntfs_device(const char *dev)
{
	unsigned char buf[16];
	int fd = open(dev, O_RDONLY);
	int n;

	if (fd < 0)
		return 0;
	n = read(fd, buf, sizeof(buf));
	close(fd);
	if (n >= 11 && memcmp(buf + 3, "NTFS    ", 8) == 0)
		return 1;
	return 0;
}

static int run_ntfs_3g(const char *dev, const char *dir)
{
	int pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		char *args[4];
		args[0] = "/bin/ntfs-3g";
		args[1] = (char *)dev;
		args[2] = (char *)dir;
		args[3] = NULL;
		execve("/bin/ntfs-3g", args, NULL);
		_exit(127);
	}
	{
		int status = 0;
		while (wait(&status) != pid)
			;
		return (status == 0) ? 0 : -1;
	}
}

static int do_one_mount(const char *dev, const char *dir, const char *type,
			unsigned long flags, void *data_ptr)
{
	int i, rc;

	mkdir(dir, 0755);

	if (type && (!strcmp(type, "ntfs") || !strcmp(type, "ntfs-3g"))) {
		return run_ntfs_3g(dev, dir);
	}

	if (type && strcmp(type, "auto") != 0) {
		return mount((char *)dev, (char *)dir, (char *)type, flags, data_ptr);
	}

	if (is_ntfs_device(dev)) {
		return run_ntfs_3g(dev, dir);
	}

	rc = -1;
	errno = EINVAL;
	for (i = 0; autotypes[i]; i++) {
		rc = mount((char *)dev, (char *)dir, (char *)autotypes[i], flags, data_ptr);
		if (rc == 0)
			return 0;
	}
	return rc;
}

static int is_already_mounted(const char *dir)
{
	FILE *f = fopen("/proc/mounts", "r");
	char line[256];

	if (!f)
		return 0;
	while (fgets(line, sizeof(line), f)) {
		char m_dev[64], m_dir[64];
		if (sscanf(line, "%63s %63s", m_dev, m_dir) == 2) {
			if (!strcmp(m_dir, dir)) {
				fclose(f);
				return 1;
			}
		}
	}
	fclose(f);
	return 0;
}

static int mount_from_fstab(const char *match_target)
{
	FILE *f = fopen("/etc/fstab", "r");
	char line[512];
	int found = 0;

	if (!f) {
		fprintf(stderr, "mount: cannot open /etc/fstab: %s\n", strerror(errno));
		return 1;
	}

	while (fgets(line, sizeof(line), f)) {
		char *p = line;
		char src[128], mnt[128], fstype[64], mntflags[128], fsmgr[256];
		unsigned long flags = MS_MGC_VAL;
		char fs_data[512];
		int n;

		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == '\0' || *p == '\n' || *p == '\r' || *p == '#')
			continue;

		fsmgr[0] = '\0';
		n = sscanf(p, "%127s %127s %63s %127s %255s",
			   src, mnt, fstype, mntflags, fsmgr);
		if (n < 4)
			continue;

		/* Always skip voldmanaged entries in mount(8) */
		if (strstr(mntflags, "voldmanaged=") || strstr(fsmgr, "voldmanaged="))
			continue;

		if (match_target) {
			if (strcmp(src, match_target) != 0 && strcmp(mnt, match_target) != 0)
				continue;
			found = 1;
		} else {
			/* mount -a: skip root, /bin, none, auto, or noauto entries */
			if (!strcmp(mnt, "/") || !strcmp(mnt, "/bin") ||
			    !strcmp(mnt, "none") || !strcmp(mnt, "auto"))
				continue;
			if (strstr(mntflags, "noauto") || strstr(fsmgr, "noauto"))
				continue;
			if (is_already_mounted(mnt))
				continue;
		}

		fs_data[0] = '\0';
		parse_options(mntflags, &flags, fs_data, sizeof(fs_data));
		if (do_one_mount(src, mnt, fstype, flags, fs_data[0] ? fs_data : 0) != 0 && match_target) {
			fprintf(stderr, "mount: %s on %s: %s\n", src, mnt, strerror(errno));
			fclose(f);
			return 1;
		}
		if (match_target)
			break;
	}

	fclose(f);
	if (match_target && !found) {
		fprintf(stderr, "mount: can't find %s in /etc/fstab\n", match_target);
		return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	char *type = 0;
	char *dev = 0, *dir = 0;
	unsigned long flags = MS_MGC_VAL;
	char fs_data[512];
	void *data_ptr = 0;
	int mount_all_flag = 0;
	int i, rc;

	fs_data[0] = '\0';

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-a")) {
			mount_all_flag = 1;
		} else if (!strcmp(argv[i], "-t")) {
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
			if (parse_options(argv[i], &flags, fs_data, sizeof(fs_data)) != 0)
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

	if (mount_all_flag)
		return mount_from_fstab(NULL);

	if (!dev && !dir)
		return show_mounts();

	if (dev && !dir)
		return mount_from_fstab(dev);

	if (fs_data[0])
		data_ptr = fs_data;

	rc = do_one_mount(dev, dir, type, flags, data_ptr);
	if (rc < 0) {
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
