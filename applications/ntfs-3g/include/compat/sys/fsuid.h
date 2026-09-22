#ifndef _COMPAT_SYS_FSUID_H
#define _COMPAT_SYS_FSUID_H
#include <sys/types.h>
int setfsuid(uid_t fsuid);
int setfsgid(gid_t fsgid);
#endif
