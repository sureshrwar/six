#include <linux/types.h>
#include <stdio.h>


int
strncmp(register const char *s1, register const char *s2, register size_t n)
{
        if (n) { 
                do {
                        if (*s1 != *s2++)
                                break;
                        if (*s1++ == '\0')
                                return 0;
                } while (--n > 0);
                if (n > 0) {
                        if (*s1 == '\0') return -1;
                        if (*--s2 == '\0') return 1;
                        return *s1 - *s2;
                }
        }
        return 0;
}       

int
strcmp(register const char *s1, register const char *s2)
{
        while (*s1 == *s2++) {
                if (*s1++ == '\0') {
                        return 0;
                }               
        }               
        if (*s1 == '\0') return -1;
        if (*--s2 == '\0') return 1;
        return *s1 - *s2;
}      
