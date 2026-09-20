#include <string.h>
#include <ctype.h>

int strcasecmp(const char *s1, const char *s2)
{
	while (*s1 && *s2) {
		int c1 = tolower(*(const unsigned char *)s1);
		int c2 = tolower(*(const unsigned char *)s2);
		if (c1 != c2)
			return c1 - c2;
		s1++;
		s2++;
	}
	return tolower(*(const unsigned char *)s1) - tolower(*(const unsigned char *)s2);
}

int strncasecmp(const char *s1, const char *s2, size_t n)
{
	while (n && *s1 && *s2) {
		int c1 = tolower(*(const unsigned char *)s1);
		int c2 = tolower(*(const unsigned char *)s2);
		if (c1 != c2)
			return c1 - c2;
		s1++;
		s2++;
		n--;
	}
	if (n == 0)
		return 0;
	return tolower(*(const unsigned char *)s1) - tolower(*(const unsigned char *)s2);
}
