#ifndef _COMPAT_WCHAR_H
#define _COMPAT_WCHAR_H
#include <stddef.h>
typedef unsigned int wint_t;
typedef struct {
    int __count;
    unsigned int __value;
} mbstate_t;
size_t mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps);
size_t wcrtomb(char *s, wchar_t wc, mbstate_t *ps);
size_t mbsrtowcs(wchar_t *dst, const char **src, size_t len, mbstate_t *ps);
int mbsinit(const mbstate_t *ps);
#endif
