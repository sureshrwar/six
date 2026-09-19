#include <stdio.h>
#include <stdarg.h>
                
int scanf(const char *format, ...)
{                       
        va_list ap;             
        int retval;
                        
        va_start(ap, format); 
                
        retval = doscan(stdin, format, ap);
                            
        va_end(ap);             
                                    
        return retval;
}                       

