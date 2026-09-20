#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

void __assert_fail(const char *assertion, const char *file, unsigned int line)
{
	fprintf(stderr, "assertion failed: %s at %s:%u\n", assertion, file, line);
	abort();
}
