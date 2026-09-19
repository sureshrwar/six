#include <stdio.h>


int ungetc(int ch, FILE *stream)    
{
        unsigned char *p;
                                                        
        if (ch == EOF  || !(stream->flags & _IOREADING))
                return EOF;
        if (stream->ptr == stream->buf) { 
                if (stream->count != 0) return EOF;
                stream->ptr++;
        }                       
        stream->count++;       
        p = --(stream->ptr);           /* ??? Bloody vax assembler !!! */
        /* ungetc() in sscanf() shouldn't write in rom */
        if (*p != (unsigned char) ch)       
                *p = (unsigned char) ch;    
        return ch;                              
}

