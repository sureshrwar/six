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

double sqrt(double x)
{
	double res;
	__asm__ ("fsqrt" : "=t" (res) : "0" (x));
	return res;
}

double sin(double x)
{
	double res;
	__asm__ ("fsin" : "=t" (res) : "0" (x));
	return res;
}

double cos(double x)
{
	double res;
	__asm__ ("fcos" : "=t" (res) : "0" (x));
	return res;
}

double atan2(double y, double x)
{
	double res;
	__asm__ ("fpatan" : "=t" (res) : "0" (x), "u" (y) : "st(1)");
	return res;
}

double atan(double x)
{
	return atan2(x, 1.0);
}

double tan(double x)
{
	return sin(x) / cos(x);
}

double log(double x)
{
	double res;
	if (x <= 0.0)
		return -HUGE_VAL;
	__asm__ ("fldln2; fxch; fyl2x" : "=t" (res) : "0" (x) : "st(1)");
	return res;
}

double exp(double x)
{
	double res;
	__asm__ (
		"fldl2e\n\t"
		"fmulp\n\t"
		"fld %%st(0)\n\t"
		"frndint\n\t"
		"fsubr %%st(0), %%st(1)\n\t"
		"fxch\n\t"
		"f2xm1\n\t"
		"fld1\n\t"
		"faddp\n\t"
		"fscale\n\t"
		"fstp %%st(1)"
		: "=t" (res) : "0" (x) : "st(1)"
	);
	return res;
}

double pow(double x, double y)
{
	if (x == 0.0)
		return 0.0;
	if (y == 0.0)
		return 1.0;
	if (y == 1.0)
		return x;
	if (x > 0.0)
		return exp(y * log(x));
	if ((long)y == y) {
		double r = exp(y * log(-x));
		return ((long)y & 1) ? -r : r;
	}
	return 0.0;
}
