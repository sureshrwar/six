#include <stdio.h>
#include <linux/types.h>



int
fflush(FILE *stream)
{
        int count, c1, i, retval = 0;

        if (!stream) {
            for(i= 0; i < FOPEN_MAX; i++)
                if (iotab[i] && fflush(iotab[i]))
                        retval = EOF;
            return retval;
        }

        if (!stream->buf
            || (!(stream->flags & _IOREADING)
                && !(stream->flags & _IOWRITING)))
                return 0;
        if (stream->flags & _IOREADING) {
                /* (void) fseek(stream, 0L, SEEK_CUR); */
                int adjust = 0;
                if (stream->buf && !(stream->flags & _IONBF))
                        adjust = stream->count;
                stream->count = 0;
                lseek(fileno(stream), (off_t) adjust, SEEK_CUR);
                if (stream->flags & _IOWRITE)
                        stream->flags &= ~(_IOREADING | _IOWRITING);
                stream->ptr = stream->buf;
                return 0;
        } else if (stream->flags & _IONBF) return 0;

        if (stream->flags & _IOREAD)               /* "a" or "+" mode */
                stream->flags &= ~_IOWRITING;

        count = stream->ptr - stream->buf;
        stream->ptr = stream->buf;

        if ( count <= 0 )
                return 0;

        if (stream->flags & _IOAPPEND) {
                if (lseek(fileno(stream), 0L, SEEK_END) == -1) {
                        stream->flags |= _IOERR;
                        return EOF;
                }
        }
        c1 = write(stream->fd, (char *)stream->buf, count);

        stream->count = 0;

        if ( count == c1 )
                return 0;

        stream->flags |= _IOERR;
        return EOF;
}

void
__cleanup(void)
{
        register int i;

        for(i= 0; i < FOPEN_MAX; i++)
                if (iotab[i] && (iotab[i]->flags & _IOWRITING))
                        (void) fflush(iotab[i]);
}

