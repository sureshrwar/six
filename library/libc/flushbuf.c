#include <stdio.h>

extern void __cleanup(void);
extern int (*_clean)(void);

static int do_write(int d, char *buf, int nbytes)
{                       
        int c;  
        while ((c = write(d, buf, nbytes)) > 0 && c < nbytes) {
                nbytes -= c;
                buf += c;
        }               
        return c > 0;           
}                               

int __flushbuf(int c, FILE * stream)
{
        _clean = __cleanup;
        if (fileno(stream) < 0) return EOF;
        if (!(stream->flags & _IOWRITE)) return EOF;
        if ((stream->flags & _IOREADING) && !feof(stream)) return EOF;

        stream->flags &= ~_IOREADING;
        stream->flags |= _IOWRITING;
        if (!(stream->flags & _IONBF)) {
                if (!stream->buf) {
                        if ((stream == stdout) && isatty(fileno(stdout))) {
                                if (!(stream->buf =
                                            (unsigned char *) malloc(BUFSIZ))) {
                                        stream->flags |= _IONBF;
                                } else {
                                        stream->flags |= _IOLBF|_IOMYBUF;
                                        stream->bufsiz = BUFSIZ;
                                        stream->count = -1;
                                }
                        } else {
                                if (!(stream->buf =
                                            (unsigned char *) malloc(BUFSIZ))) {
                                        stream->flags |= _IONBF;
                                } else {
                                        stream->flags |= _IOMYBUF;
                                        stream->bufsiz = BUFSIZ;
                                        if (!(stream->flags & _IOLBF)) 
                                                stream->count = BUFSIZ - 1;
                                        else    stream->count = -1;
                                }
                        }
                        stream->ptr = stream->buf;
                }
        }
        
        if (stream->flags & _IONBF) {
                char c1 = c;
                
                stream->count = 0;
                if (stream->flags & _IOAPPEND) {
                        if (lseek(fileno(stream), 0L, SEEK_END) == -1) {
                                stream->flags |= _IOERR;
                                return EOF;
                        }
                }
                return c;
        } else if (stream->flags & _IOLBF) {
                *stream->ptr++ = c;
                if (c == '\n' || stream->count == -stream->bufsiz) {
                        if (stream->flags & _IOAPPEND) {
                                if (lseek(fileno(stream), 0L, SEEK_END) == -1) {
                                        stream->flags |= _IOERR;
                                        return EOF;
                                }
                        }
                        if (! do_write(fileno(stream), (char *)stream->buf,
                                        -stream->count)) {
                                stream->flags |= _IOERR;
                                return EOF;
                        } else {
                                stream->ptr  = stream->buf;
                                stream->count = 0;
                        }
                }
        } else {
                int count = stream->ptr - stream->buf;

                stream->count = stream->bufsiz - 1;
                stream->ptr = stream->buf + 1;

                if (count > 0) {
                        if (stream->flags & _IOAPPEND) {
                                if (lseek(fileno(stream), 0L, SEEK_END) == -1) {
                                        stream->flags |= _IOERR;
                                        return EOF;
                                }
                        }
                        if (! do_write(fileno(stream), (char *)stream->buf, count)) {
                                *(stream->buf) = c;
                                stream->flags |= _IOERR;
                                return EOF;
                        }
                }
                *(stream->buf) = c;
        }
        return c;
}

