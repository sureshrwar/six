#include <stdio.h>
#include <stdarg.h>
                
int fscanf(FILE *fp, const char *format, ...)
{                       
        va_list ap;             
        int retval;
                        
        va_start(ap, format); 
                
        retval = doscan(fp, format, ap);
                            
        va_end(ap);             
                                    
        return retval;
}                       

