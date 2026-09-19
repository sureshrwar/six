#include <stdarg.h>
#include <stdio.h>

int fprintf(FILE *fp, const char *fmt, ...)
{
        va_list args;
        int i;

        va_start(args, fmt);
        i=vsprintf(0, fp, fmt, args);
        va_end(args);
        return i;
}
