/*
 * df.c - Report filesystem disk space usage for SIX (/bin/df)
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <linux/string.h>
#include <linux/unistd.h>
#include <asm/statfs.h>
#include <stat.h>

static void format_human(long kbytes, char *out)
{
	if (kbytes >= 1024 * 1024)
		sprintf(out, "%ld.%ldG", kbytes / (1024 * 1024), (kbytes % (1024 * 1024)) / (1024 * 100));
	else if (kbytes >= 1024)
		sprintf(out, "%ldM", kbytes / 1024);
	else
		sprintf(out, "%ldK", kbytes);
}

/*
 * Name the device a path lives on.
 *
 * This used to be the literal string "/dev/hda", which was true for as
 * long as there was only one disk and nothing but / was ever mounted.
 * Both of those stopped being true when /dev/hdb arrived.
 *
 * struct statfs carries no device, so take it from stat(2)'s st_dev.  The
 * hd driver splits its minor into a drive number in the top two bits and a
 * partition in the bottom six -- DEVICE_NR() in <linux/blk.h> -- and SIX
 * has no partitions, so the drive letter is simply minor >> 6.  Anything
 * that is not major 3 gets the major:minor form the kernel itself prints,
 * which is what kdevname() in fs/devices.c does.
 */
static void device_name(const char *path, long f_type, char *out, int outlen)
{
	struct stat st;
	int major, minor;

	if (stat((char *)path, &st) != 0) {
		snprintf(out, outlen, "-");
		return;
	}

	major = (st.st_dev >> 8) & 0xff;
	minor = st.st_dev & 0xff;

	if (major == 3)
		snprintf(out, outlen, "/dev/hd%c", 'a' + (minor >> 6));
	else if (major == 8)
		snprintf(out, outlen, "/dev/sda%d", minor);
	else if (major == 62)
		snprintf(out, outlen, "/dev/dm-%d", minor);
	else if (major == 0 && (unsigned long)f_type == 0x65735546UL)
		snprintf(out, outlen, "fuse");
	else if ((unsigned long)f_type == 0x01021994UL)
		snprintf(out, outlen, "tmpfs");
	else
		snprintf(out, outlen, "%02x:%02x", major, minor);
}

static int show_df(const char *path, int human)
{
	struct statfs s;
	char dev[16];
	long total_k, free_k, used_k, avail_k;
	int pct = 0;

	if (statfs((char *)path, &s) != 0) {
		perror(path);
		return 1;
	}

	device_name(path, s.f_type, dev, sizeof(dev));

	total_k = (s.f_blocks * s.f_bsize) / 1024;
	free_k  = (s.f_bfree * s.f_bsize) / 1024;
	avail_k = (s.f_bavail * s.f_bsize) / 1024;
	used_k  = total_k - free_k;

	if (total_k > 0)
		pct = (int)((used_k * 100) / total_k);

	if (human) {
		char s_tot[16], s_used[16], s_avail[16];
		format_human(total_k, s_tot);
		format_human(used_k, s_used);
		format_human(avail_k, s_avail);
		printf("%-15s %8s %8s %8s %4d%% %s\n",
		       dev, s_tot, s_used, s_avail, pct, path);
	} else {
		printf("%-15s %9ld %9ld %9ld %4d%% %s\n",
		       dev, total_k, used_k, avail_k, pct, path);
	}
	return 0;
}

int main(int argc, char **argv)
{
	int human = 0;
	const char *path = NULL;
	int i, rc;
	static const char *extra_mounts[] = {
		"/bin",
		"/tmp",
		"/aux/storage-1",
		"/aux/linear",
		"/aux/crypt",
		"/mnt/media_rw/4A8F-9C21",
		"/mnt/media_rw/5B9E-7D31",
		"/mnt/media_rw/6A1B-8E42",
		"/mnt/expand/CRYPT-8A01",
		NULL
	};

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0) human = 1;
		else path = argv[i];
	}

	if (human)
		printf("%-15s %8s %8s %8s %5s %s\n",
		       "Filesystem", "Size", "Used", "Avail", "Use%", "Mounted on");
	else
		printf("%-15s %9s %9s %9s %5s %s\n",
		       "Filesystem", "1K-blocks", "Used", "Available", "Use%", "Mounted on");

	if (path)
		return show_df(path, human);

	rc = show_df("/", human);
	{
		struct stat st_root, st_sub;
		if (stat("/", &st_root) == 0) {
			for (i = 0; extra_mounts[i]; i++) {
				if (stat((char *)extra_mounts[i], &st_sub) == 0 &&
				    st_sub.st_dev != st_root.st_dev) {
					show_df(extra_mounts[i], human);
				}
			}
		}
	}
	return rc;
}
