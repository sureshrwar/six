#include <stdio.h>
#include <stdarg.h>
#include <string.h>

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
	char tmp[4096];
	int ret;

	ret = vsprintf(tmp, fmt, ap);
	if (size > 0 && buf != NULL) {
		if ((size_t)ret < size) {
			memcpy(buf, tmp, ret + 1);
		} else {
			memcpy(buf, tmp, size - 1);
			buf[size - 1] = '\0';
		}
	}
	return ret;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = vsnprintf(buf, size, fmt, ap);
	va_end(ap);
	return ret;
}
