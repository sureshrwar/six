
#include <stdio.h>
#include <linux/types.h>

size_t strlen(const char *org)
{
        register const char *s = org;

        while (*s++)
                /* EMPTY */ ; 

        return --s - org;
}

size_t strnlen(const char * s, size_t count)
{
        const char *sc;

        for (sc = s; count-- && *sc != '\0'; ++sc)
                /* nothing */;
        return sc - s;
}

