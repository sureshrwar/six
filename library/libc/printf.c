#include <stdarg.h>
#include <stdio.h>

int printf(const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	i = vfprintf(stdout, fmt, args);
	va_end(args);
	return i;
}
