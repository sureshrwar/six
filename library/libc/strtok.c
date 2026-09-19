

int
strspn(const char *string, const char *in)
{
        register const char *s1, *s2;
        
        for (s1 = string; *s1; s1++) {
                for (s2 = in; *s2 && *s2 != *s1; s2++)
                        /* EMPTY */ ;
                if (*s2 == '\0')
                        break;         
        }
        return s1 - string;
}       

char *
strpbrk(register const char *string, register const char *brk)
{
        register const char *s1;
        
        while (*string) {
                for (s1 = brk; *s1 && *s1 != *string; s1++)
                        /* EMPTY */ ;
                if (*s1)
                        return (char *)string;
                string++;
        }
        return (char *)0;
}               

char *
strtok(register char *string, const char *separators)
{
        register char *s1, *s2;
        static char *savestring;

        if (string == 0) {
                string = savestring;
                if (string == 0) return (char *)0;
        }                              

        s1 = string + strspn(string, separators); 
        if (*s1 == '\0') {          
                savestring = 0;
                return (char *)0;
        }                        

        s2 = strpbrk(s1, separators);
        if (s2 != 0)
                *s2++ = '\0';  
        savestring = s2; 
        return s1;
}

