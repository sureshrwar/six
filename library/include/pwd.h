#ifndef _PWD_H
#define _PWD_H

#include <linux/types.h>

struct passwd {
  char *pw_name;                /* login name */
  uid_t pw_uid;                 /* uid corresponding to the name */
  gid_t pw_gid;                 /* gid corresponding to the name */
  char *pw_dir;                 /* user's home directory */
  char *pw_shell;               /* name of the user's shell */

  /* The following members are not defined by POSIX. */
  char *pw_passwd;              /* password information */
  char *pw_gecos;               /* just in case you have a GE 645 around */
};

#endif /* _PWD_H */

