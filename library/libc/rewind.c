#include <stdio.h>

void rewind(FILE *stream)
{
        (void) fseek(stream, 0L, SEEK_SET);
        clearerr(stream);
}

