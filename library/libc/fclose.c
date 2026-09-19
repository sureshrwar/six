
#include <stdio.h>


int fclose(FILE *fp)
{
        register int i, retval = 0;

        for (i=0; i<FOPEN_MAX; i++)
                if (fp == iotab[i]) {
                        iotab[i] = 0;
                        break;
                }
        if (i >= FOPEN_MAX)
                return EOF;
        if (fflush(fp)) retval = EOF;
        if (close(fileno(fp))) retval = EOF;
        if ((fp->flags &_IOMYBUF) && fp->buf )
                free((void *)fp->buf);
        if (fp != stdin && fp != stdout && fp != stderr)
                free((void *)fp);
        return retval;
}

