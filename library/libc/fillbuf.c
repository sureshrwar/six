#include <stdio.h>

int __fillbuf(register FILE *stream)
{
        static unsigned char ch[FOPEN_MAX];
        register int i;
        
        stream->count = 0;
        if (fileno(stream) < 0) return EOF;
        if (stream->flags & (_IOEOF | _IOERR )) return EOF;
        if (!(stream->flags & _IOREAD))
             { stream->flags |= _IOERR; return EOF; }
        if (stream->flags & _IOWRITING)
             { stream->flags |= _IOERR; return EOF; }

        if (!(stream->flags & _IOREADING))
                stream->flags |= _IOREADING;
 
        if (!(stream->flags & _IONBF) && !stream->buf) {
                stream->buf = (unsigned char *) malloc(BUFSIZ);
                if (!stream->buf) {
                        stream->flags |= _IONBF;
                }
                else {
                        stream->flags |= _IOMYBUF;
                        stream->bufsiz = BUFSIZ;
                }
        }

        /* flush line-buffered output when filling an input buffer */
        for (i = 0; i < FOPEN_MAX; i++) {
                if (iotab[i] && (iotab[i]->flags & _IOLBF))
                        if (iotab[i]->flags & _IOWRITING)
                                (void) fflush(iotab[i]);
        }

        if (!stream->buf) {
                stream->buf = &ch[fileno(stream)];
                stream->bufsiz = 1;
        }
        stream->ptr = stream->buf;
        stream->count = read(stream->fd, (char *)stream->buf, stream->bufsiz);

        if (stream->count <= 0){
                if (stream->count == 0) {
                        stream->flags |= _IOEOF;
                }
                else
                        stream->flags |= _IOERR;

                return EOF;
        }
        stream->count--;

        return *stream->ptr++;
}

