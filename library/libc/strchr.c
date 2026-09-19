

char *
strchr(register const char *s, register int c)
{
        c = (char) c; 

        while (c != *s) {
                if (*s++ == '\0') return 0;
        }               
        return (char *)s;
}                       

char *
strrchr(register const char *s, int c)
{
        register const char *result = 0;

        c = (char) c;           

        do {
                if (c == *s)
                        result = s;
        } while (*s++ != '\0');

        return (char *)result;
}


