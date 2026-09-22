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
 */
static int remove_one(const char *path)
{
	if (unlink((char *)path) == 0)
		return 0;

	/*
	 * unlink() on a directory reports EISDIR, but EPERM is the
	 * POSIX-blessed answer and parts of the 2.0 VFS still use it, so
	 * accept either before retrying the target as a directory.  If the
	 * retry also fails we fall through and report *its* errno:
	 * "Directory not empty" is far more useful than unlink()'s
	 * "Not owner".
	 */
	if (errno == EISDIR || errno == EPERM) {
		if (rmdir((char *)path) == 0)
			return 0;
	}

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
