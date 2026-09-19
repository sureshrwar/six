#include <linux/types.h>
#include <errno.h>
#include <linux/limits.h>
#include <stdlib.h>
#include <stdarg.h>

#undef NULL
#define NULL 0			/* kludge for ACK not understanding void * */

#define MAX_NUM_ARGS 512	/* maximum number of arguments to execvp */

extern char **envlist;		/* environment pointer */

int execlp(const char *file, ...)
{
  register va_list argp;
  register int result;

  va_start(argp, file);

  result = execvp(file, (char **) argp);
  va_end(argp);
  return(result);
}

int execvp(const char *file, char *const argv[])
{
  int i, best_errno;
  char **envtop;
  size_t flength;
  char *searchpath;
  size_t slength;
  char *split;
  char execpath[PATH_MAX + 1];
  char *arg2[MAX_NUM_ARGS + 3];	/* place to copy argv */

  /* POSIX requires argv to be immutable.  Unfortunately, we have to change it
   * during execution.  To keep POSIX happy, a copy is made and the copy 
   * changed.  The question arises: how big should the copy be?  Allocating
   * space dynamically requires using malloc, which itself takes up a lot
   * of space.  The solution chosen here is to limit the number of arguments
   * to MAX_NUM_ARGS and set this value fairly high.  This solution is simpler
   * and is probably adequate.  Only programs with huge numbers of very short
   * arguments will get an error (if the arguments are large, ARG_MAX will
   * be exceeded.
   */

  if (strchr(file, '/') != NULL || (searchpath = getenv("PATH")) == NULL)
	searchpath = "";
  flength = strlen(file);
  best_errno = ENOENT;

  while (1) {
	split = strchr(searchpath, ':');
	if (split == NULL)
		slength = strlen(searchpath);
	else
		slength = split - searchpath;
	if (slength + flength >= sizeof execpath - 2) {
		errno = ENAMETOOLONG;	/* too bad if premature */
		return(-1);
	}
	strncpy(execpath, searchpath, slength);
	if (slength != 0) execpath[slength++] = '/';
	strcpy(execpath + slength, file);

	/* Don't try to avoid execv() for non-existent files, since the Minix
	 * shell doesn't, and it is not clear whether access() or stat() work
	 * right when this code is set-uid.
	 */
	execv(execpath, argv);
	switch (errno) {
	    case EACCES:
		best_errno = errno;	/* more useful than ENOENT */
	    case ENOENT:
		if (split == NULL) {
			/* No more path components. */
			errno = best_errno;
			return(-1);
		}
		searchpath = split + 1;	/* try next in path */
		break;
	    case ENOEXEC:
		/* Assume a command file and invoke sh(1) on it.  Replace arg0
		 * (which is usually a short name for the command) by the full
		 * name of the command file.
		 */

		/* Copy the arg pointers from argv to arg2,  moving them up by
		 * 1, overlaying the assumed NULL at the end, to make room 
		 * for "sh" at the beginning.
		 */
		i = 0;
		if (argv != NULL)
		{
			while (argv[i] != 0) {
				if (i >= MAX_NUM_ARGS) {
					/* Copy failed.  Not enough room. */
					errno = ENOEXEC;
					return(-1);
				}
				arg2[i + 1] = argv[i];
				i++;
			}
		}
		arg2[0] = "sh";		/* exec the shell */
		arg2[1] = execpath;	/* full path */
		arg2[i + 1] = NULL;	/* terminator */

		/* Count the environment pointers. */
		for (envtop = envlist; *envtop != NULL; ) envtop++;

		/* Try only /bin/sh, like the Minix shell.  Lose if the user
		 * has a different shell or the command has #!another/shell.
		 */
		execve("/bin/sh", arg2, envlist, i + 1, (int)(envtop - envlist));

		/* Oops, no shell?   Give up. */
		errno = ENOEXEC;
		return(-1);
	    default:
		return(-1);	/* probably  ENOMEM or E2BIG */
	}
  }
}
