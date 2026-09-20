#include <math.h>
#include <ctype.h>
#include <stdlib.h>

double strtod(const char *nptr, char **endptr)
{
	const char *s = nptr;
	double val = 0.0;
	int sign = 1;
	double frac = 0.1;
	int has_digits = 0;
	int exp_sign = 1;
	int exp_val = 0;

	while (isspace((unsigned char)*s))
		s++;

	if (*s == '-') {
		sign = -1;
		s++;
	} else if (*s == '+') {
		s++;
	}

	while (isdigit((unsigned char)*s)) {
		val = val * 10.0 + (*s - '0');
		has_digits = 1;
		s++;
	}

	if (*s == '.') {
		s++;
		while (isdigit((unsigned char)*s)) {
			val += (*s - '0') * frac;
			frac *= 0.1;
			has_digits = 1;
			s++;
		}
	}

	if (!has_digits) {
		if (endptr)
			*endptr = (char *)nptr;
		return 0.0;
	}

	if (*s == 'e' || *s == 'E') {
		const char *exp_start = s;
		s++;
		if (*s == '-') {
			exp_sign = -1;
			s++;
		} else if (*s == '+') {
			s++;
		}
		if (isdigit((unsigned char)*s)) {
			while (isdigit((unsigned char)*s)) {
				exp_val = exp_val * 10 + (*s - '0');
				s++;
			}
			while (exp_val > 0) {
				if (exp_sign > 0)
					val *= 10.0;
				else
					val /= 10.0;
				exp_val--;
			}
		} else {
			s = exp_start;
		}
	}

	if (endptr)
		*endptr = (char *)s;

	return sign * val;
}

float strtof(const char *nptr, char **endptr)
{
	return (float)strtod(nptr, endptr);
}

long double strtold(const char *nptr, char **endptr)
{
	return (long double)strtod(nptr, endptr);
}

double atof(const char *nptr)
{
	return strtod(nptr, NULL);
}
