
#include <stdio.h>

int puts(register const char *s)
{
        register FILE *file = stdout;
        register int i = 0;

        while (*s) {
                int c = (unsigned char)*s++;
                if (putc(c, file) == EOF) return EOF;
                else i++;
        }
        if (putc('\n', file) == EOF) return EOF;
        return i + 1;
}

