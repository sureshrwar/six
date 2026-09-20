#ifndef _ASSERT_H
#define _ASSERT_H

#include <stdio.h>
#include <stdlib.h>

#ifdef NDEBUG
#define assert(expr) ((void)0)
#else
#define assert(expr) \
	((expr) ? (void)0 : \
	 (__assert_fail(#expr, __FILE__, __LINE__)))
#endif

void __assert_fail(const char *assertion, const char *file, unsigned int line);

#endif
