/*
 * mkdir.c - Make directories for SIX (/bin/mkdir)
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <linux/string.h>
#include <linux/unistd.h>
#include <stat.h>

/*
 * Report a failed system call.
 *
 * The stubs in library/sys/ return the kernel's negative errno and never
 * assign to errno, so perror() on its own prints whatever happened to be
 * there -- in practice "Error 0", since nothing ever sets it.  Fill it in
 * from the return value first.  Same pattern as applications/rm/rm.c.
 */
static void fail(const char *what, int rc)
{
	errno = (rc < 0) ? -rc : rc;
	perror(what);
}

static int make_parents(char *path, int mode)
{
	struct stat st;
	char *p = path;
	int rc;

	if (*p == '/') p++;

	while (*p) {
		if (*p == '/') {
			*p = '\0';
			if (stat(path, &st) != 0) {
				rc = mkdir(path, mode);
				if (rc != 0) {
					fail(path, rc);
					*p = '/';
					return 1;
				}
			}
			*p = '/';
		}
		p++;
	}

	/*
	 * The final component.  This used to be stat(path, NULL): a null
	 * buffer, so the call could only ever fail, and the mkdir below ran
	 * unconditionally.  That made "mkdir -p" report EEXIST for a
	 * directory that was already there -- which is precisely the case
	 * -p exists to tolerate, and precisely what happens on the second
	 * and every subsequent boot if the mkdir lives in /etc/rc.
	 */
	if (stat(path, &st) != 0) {
		rc = mkdir(path, mode);
		if (rc != 0) {
			fail(path, rc);
			return 1;
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	int p_flag = 0;
	int i, rc = 0;
	int mode = 0755;

	if (argc < 2) {
		fprintf(stderr, "Usage: mkdir [-p] <directory...>\n");
		return 1;
	}

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-p") == 0) {
			p_flag = 1;
			continue;
		}
		if (p_flag) {
			char tmp[256];
			strncpy(tmp, argv[i], sizeof(tmp) - 1);
			tmp[sizeof(tmp) - 1] = '\0';
			if (make_parents(tmp, mode) != 0)
				rc = 1;
		} else {
			int ret = mkdir(argv[i], mode);
			if (ret != 0) {
				fail(argv[i], ret);
				rc = 1;
			}
		}
	}

	return rc;
}
