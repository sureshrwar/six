#include <math.h>

double ldexp(double x, int exp)
{
	double factor = 2.0;

	if (exp < 0) {
		factor = 0.5;
		exp = -exp;
	}
	while (exp > 0) {
		if (exp & 1)
			x *= factor;
		factor *= factor;
		exp >>= 1;
	}
	return x;
}

double frexp(double x, int *exp)
{
	int e = 0;

	if (x == 0.0) {
		if (exp)
			*exp = 0;
		return 0.0;
	}
	if (x < 0.0)
		return -frexp(-x, exp);

	while (x >= 1.0) {
		x *= 0.5;
		e++;
	}
	while (x < 0.5) {
		x *= 2.0;
		e--;
	}
	if (exp)
		*exp = e;
	return x;
}

double fabs(double x)
{
	return (x < 0.0) ? -x : x;
}

double floor(double x)
{
	long n = (long)x;

	if (x < 0.0 && x != (double)n)
		return (double)(n - 1);
	return (double)n;
}

double ceil(double x)
{
	long n = (long)x;

	if (x > 0.0 && x != (double)n)
		return (double)(n + 1);
	return (double)n;
}
