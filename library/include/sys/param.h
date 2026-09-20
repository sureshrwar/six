#ifndef _SYS_PARAM_H
#define _SYS_PARAM_H

#include <linux/limits.h>

#ifndef MAXPATHLEN
#ifdef PATH_MAX
#define MAXPATHLEN PATH_MAX
#else
#define MAXPATHLEN 1024
#endif
#endif

#ifndef HZ
#define HZ 100
#endif

#endif
