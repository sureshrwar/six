
#include <stdio.h>
#include <linux/types.h>


char *
strcat(char *ret, register const char *s2)
{
        register char *s1 = ret; 

        while (*s1++ != '\0')
                /* EMPTY */ ; 
        s1--;
        while (*s1++ = *s2++)
                /* EMPTY */ ;
        return ret;
}

char *
strncat(char *ret, register const char *s2, size_t n)
{ 
        register char *s1 = ret;
        
        if (n > 0) {
                while (*s1++)
                        /* EMPTY */ ;
                s1--;
                while (*s1++ = *s2++)  {
                        if (--n > 0) continue;
                        *s1 = '\0';
                        break;
                }
                return ret;
        } else return s1;
} 

