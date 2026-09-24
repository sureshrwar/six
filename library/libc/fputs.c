#include <stdio.h>


int fputs(register const char *s, register FILE *stream)
{
        register int i = 0;

        while (*s) {
                int c = (unsigned char)*s++;
                if (putc(c, stream) == EOF) return EOF;
                else i++;
        }

        return i;
}

