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
#include <errno.h>
#include <linux/unistd.h>
#include <sys/mount.h>

int main(int argc, char **argv)
{
	int i, ret = 0;

	if (argc < 2) {
		fprintf(stderr, "Usage: umount <dir|device...>\n");
		return 1;
	}

	for (i = 1; i < argc; i++) {
		int rc = umount(argv[i]);

		if (rc < 0) {
			/*
			 * The stubs in library/sys/ return the kernel's
			 * negative errno and never set errno, so perror()
			 * needs it filled in by hand.
			 */
			errno = -rc;
			perror(argv[i]);
			ret = 1;
		}
	}

	return ret;
}
