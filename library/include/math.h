#ifndef _LIBRARY_MATH_H
#define _LIBRARY_MATH_H

#define HUGE_VAL  (__builtin_huge_val())
#define HUGE_VALF (__builtin_huge_valf())
#define HUGE_VALL (__builtin_huge_vall())

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E  2.71828182845904523536
#endif

double strtod(const char *nptr, char **endptr);
float strtof(const char *nptr, char **endptr);
long double strtold(const char *nptr, char **endptr);
double ldexp(double x, int exp);
double frexp(double x, int *exp);
double fabs(double x);
double floor(double x);
double ceil(double x);

#endif /* _LIBRARY_MATH_H */
