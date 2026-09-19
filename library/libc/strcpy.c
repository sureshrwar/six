



char *
strcpy(char *ret, register const char *s2)
{
        register char *s1 = ret;

        while (*s1++ = *s2++)
                /* EMPTY */ ;

        return ret;
}

char *
strncpy(char *ret, register const char *s2, register int n)
{
        register char *s1 = ret;

        if (n>0) {
                while((*s1++ = *s2++) && --n > 0)
                        /* EMPTY */ ;
                if ((*--s2 == '\0') && --n > 0) {
                        do {           
                                *s1++ = '\0';
                        } while(--n > 0); 
                }                   
        }                 
        return ret;
}                                


