#ifndef _COMPAT_SYS_SYSMACROS_H
#define _COMPAT_SYS_SYSMACROS_H
#ifndef major
#define major(dev) (((unsigned int)(dev) >> 8) & 0xff)
#endif
#ifndef minor
#define minor(dev) ((unsigned int)(dev) & 0xff)
#endif
#ifndef makedev
#define makedev(maj, min) ((((unsigned int)(maj) & 0xff) << 8) | ((unsigned int)(min) & 0xff))
#endif
#endif
