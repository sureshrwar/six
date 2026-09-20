#include <string.h>

char *strstr(const char *haystack, const char *needle)
{
	size_t nlen;
	if (!haystack || !needle)
		return NULL;
	if (*needle == '\0')
		return (char *)haystack;
	nlen = strlen(needle);
	while (*haystack) {
		if (*haystack == *needle && strncmp(haystack, needle, nlen) == 0)
			return (char *)haystack;
		haystack++;
	}
	return NULL;
}
