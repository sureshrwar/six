#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

extern int doscan(FILE *stream, const char *format, va_list ap);

int sscanf(const char *str, const char *format, ...)
{
	FILE f;
	va_list ap;
	int retval;

	if (!str || !format)
		return 0;

	f.count = strlen(str);
	f.fd = -1;
	f.flags = _IOREAD | _IOREADING;
	f.bufsiz = f.count;
	f.ptr = (unsigned char *)str;
	f.buf = (unsigned char *)str;

	va_start(ap, format);
	retval = doscan(&f, format, ap);
	va_end(ap);

	return retval;
}
