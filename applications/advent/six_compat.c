#include <stdlib.h>

int abs(int x)
{
	return x < 0 ? -x : x;
}

int sscanf(const char *str, const char *fmt, int *out)
{
	if (!str || !out)
		return 0;
	*out = atoi((char *)str);
	return 1;
}
