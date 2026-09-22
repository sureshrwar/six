/*
 * rm.c - Remove files (and empty directories) for SIX (/bin/rm)
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <linux/unistd.h>

/*
 * Historically this program called rmdir() for everything, which meant
 * /bin/rm could only ever delete *empty directories* -- removing an ordinary
 * file failed with ENOTDIR and printed "Operation failed".  Files are removed
 * with unlink(); we still fall back to rmdir() when the target turns out to
 * be a directory, so the old "rm <empty-dir>" behaviour keeps working.
 *
 * Note that the stubs in library/sys/ return the kernel's negative error code
 * directly and do not touch errno, so we set it ourselves before calling
 * perror().
 */
static int remove_one(const char *path)
{
	int rc;

	rc = unlink((char *)path);
	if (rc == 0)
		return 0;

	/*
	 * unlink() on a directory reports EISDIR, but EPERM is the
	 * POSIX-blessed answer and parts of the 2.0 VFS still use it, so
	 * accept either before retrying the target as a directory.  If the
	 * retry also fails, report *its* error: "Directory not empty" is far
	 * more useful than unlink()'s "Not owner".
	 */
	if (rc == -EISDIR || rc == -EPERM) {
		int dir_rc = rmdir((char *)path);
		if (dir_rc == 0)
			return 0;
		rc = dir_rc;
	}

	errno = (rc < 0) ? -rc : rc;
	perror(path);
	return 1;
}

int main(int argc, char **argv)
{
	int i, rc = 0;

	if (argc < 2) {
		fprintf(stderr, "Usage: rm <file...>\n");
		return 2;
	}

	for (i = 1; i < argc; i++)
		rc |= remove_one(argv[i]);

	return rc;
}
