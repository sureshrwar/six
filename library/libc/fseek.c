#include <stdio.h>


int fseek(FILE *stream, long int offset, int whence)
{
        int adjust = 0;
        long pos;

        stream->flags &= ~(_IOEOF | _IOERR);
        /* Clear both the end of file and error flags */

        if (stream->flags & _IOREADING) {
                if (whence == SEEK_CUR
                    && stream->buf
                    && !(stream->flags & _IONBF))
                        adjust = stream->count;
                stream->count = 0;
        } else if (stream->flags & _IOWRITING) {
                fflush(stream);
        } else  /* neither reading nor writing. The buffer must be empty */
                /* EMPTY */ ;

        pos = lseek(fileno(stream), offset - adjust, whence);
        if ((stream->flags & _IOREAD) && (stream->flags & _IOWRITE))
                stream->flags &= ~(_IOREADING | _IOWRITING);

        stream->ptr = stream->buf;
        return ((pos == -1) ? -1 : 0);
}

