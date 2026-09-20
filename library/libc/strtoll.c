#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>

#define between(a, c, z)  ((unsigned) ((c) - (a)) <= (unsigned) ((z) - (a)))

static unsigned long long
string2longlong(const char *nptr, char **endptr, int base, int is_signed)
{
	unsigned int v;
	unsigned long long val = 0;
	int c;
	int sign = 1;
	const char *startnptr = nptr, *nrstart;

	if (endptr)
		*endptr = (char *)nptr;
	while (isspace((unsigned char)*nptr))
		nptr++;
	c = *nptr;

	if (c == '-' || c == '+') {
		if (c == '-')
			sign = -1;
		nptr++;
	}
	nrstart = nptr;

	if (base == 0) {
		if (*nptr == '0') {
			if (*++nptr == 'x' || *nptr == 'X') {
				base = 16;
				nptr++;
			} else {
				base = 8;
			}
		} else {
			base = 10;
		}
	} else if (base == 16 && *nptr == '0' && (nptr[1] == 'x' || nptr[1] == 'X')) {
		nptr += 2;
	}

	for (;;) {
		c = *nptr;
		if (between('0', c, '9')) {
			v = c - '0';
		} else if (between('a', c, 'z')) {
			v = c - 'a' + 10;
		} else if (between('A', c, 'Z')) {
			v = c - 'A' + 10;
		} else {
			break;
		}
		if (v >= (unsigned int)base)
			break;
		val = (val * base) + v;
		nptr++;
	}

	if (endptr) {
		if (nrstart == nptr)
			*endptr = (char *)startnptr;
		else
			*endptr = (char *)nptr;
	}

	return (is_signed && sign < 0) ? -val : val;
}

long long strtoll(const char *nptr, char **endptr, int base)
{
	return (long long)string2longlong(nptr, endptr, base, 1);
}

unsigned long long strtoull(const char *nptr, char **endptr, int base)
{
	return string2longlong(nptr, endptr, base, 0);
}
