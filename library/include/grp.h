#ifndef _GRP_H
#define _GRP_H

#include <linux/types.h>

struct  group {
  char *gr_name;                /* the name of the group */
  char *gr_passwd;              /* the group passwd */
  gid_t gr_gid;                 /* the numerical group ID */
  char **gr_mem;                /* a vector of pointers to the members */
};

void endgrent(void);
int setgrent(void);
void setgrfile(const char *file);
struct group *getgrent(void);
struct group *getgrgid(gid_t gid);
struct group *getgrnam(const char *name);

#endif /* _GRP_H */

