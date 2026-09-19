#include <stdarg.h>
#include <stdio.h>

extern vsprintf(char *buf, FILE *fp, const char *fmt, va_list args);

int sprintf(char * buf, const char *fmt, ...)
{
        va_list args;
        int i;

        va_start(args, fmt);
        i=vsprintf(buf, 0, fmt,args);
        va_end(args);
        return i;
}


