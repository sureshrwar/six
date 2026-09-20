#include <string.h>
#include <stdlib.h>

char *strdup(const char *s)
{
	size_t len;
	char *copy;

	if (!s)
		return NULL;
	len = strlen(s) + 1;
	copy = malloc(len);
	if (!copy)
		return NULL;
	memcpy(copy, s, len);
	return copy;
}
