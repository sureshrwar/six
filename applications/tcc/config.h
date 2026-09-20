#ifndef _CONFIG_H
#define _CONFIG_H

#define TCC_VERSION "0.9.27"
#define CONFIG_TCCDIR "/usr/lib/tcc"
#define CONFIG_TCC_CRTPREFIX "/usr/lib"
#define CONFIG_TCC_SYSINCLUDEPATHS "{B}/include:/usr/include"
#define CONFIG_TCC_LIBPATHS "/usr/lib"
#define CONFIG_TCC_STATIC 1
#define CONFIG_TCCBOOT 1
#undef CONFIG_TCC_BCHECK
#undef CONFIG_TCC_BACKTRACE

#endif
