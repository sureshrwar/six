#ifndef _COMPAT_UTIME_H
#define _COMPAT_UTIME_H
#include <linux/utime.h>
int utime(const char *filename, const struct utimbuf *times);
#endif
