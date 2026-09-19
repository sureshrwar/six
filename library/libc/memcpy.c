#include <linux/types.h>

void * memcpy(void *dest, const void *src, size_t count)
{                              
        char *tmp = (char *) dest, *s = (char *) src;

        while (count--)
                *tmp++ = *s++;

        return dest;
}

