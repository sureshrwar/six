#include <linux/types.h>
#include <stdio.h>
#include <loc_incl.h>

#if     (SEEK_CUR != 1) || (SEEK_SET != 0) || (SEEK_END != 2)
#error SEEK_* values are wrong
#endif

off_t _lseek(int fildes, off_t offset, int whence);

long ftell(FILE *stream)              
{
        long result;                     
        int adjust = 0;           

        if (stream->flags & _IOREADING)
                adjust = -stream->count; 
        else if ((stream->flags & _IOWRITING)      
                    && stream->buf
                    && !(stream->flags & _IONBF))  
                adjust = stream->ptr - stream->buf;
        else adjust = 0;

        result = lseek(fileno(stream), (off_t)0, SEEK_CUR);

        if ( result == -1 )
                return result;

        result += (long) adjust;
        return result;
}

