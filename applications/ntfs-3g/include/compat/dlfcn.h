#ifndef _COMPAT_DLFCN_H
#define _COMPAT_DLFCN_H
#define RTLD_NOW 2
#define RTLD_LAZY 1
static inline void *dlopen(const char *f, int m) { (void)f; (void)m; return 0; }
static inline void *dlsym(void *h, const char *s) { (void)h; (void)s; return 0; }
static inline int dlclose(void *h) { (void)h; return 0; }
static inline char *dlerror(void) { return "dlopen disabled"; }
#endif
