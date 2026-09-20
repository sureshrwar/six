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
double sqrt(double x);
double sin(double x);
double cos(double x);
double tan(double x);
double atan(double x);
double atan2(double y, double x);
double log(double x);
double exp(double x);
double pow(double x, double y);
double modf(double x, double *iptr);

#endif /* _LIBRARY_MATH_H */
