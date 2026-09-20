/*
 * stdiooooooo!
 */
#ifndef _LIBRARY_STDIO_H
#define _LIBRARY_STDIO_H

#include <stddef.h>
#include <stdarg.h>

typedef struct iobuf {
        int             count;
        int             fd;
        int             flags;
        int             bufsiz;
        unsigned char   *ptr;
        unsigned char   *buf;
} FILE; 

#define stdin           (&__stdin)
#define stdout          (&__stdout)
#define stderr          (&__stderr)

#define	_NFILE		20
#define FOPEN_MAX       _NFILE  
#define FILENAME_MAX    1024    /* max # of characters in a path name */

#define SEEK_SET        0
#define SEEK_CUR        1
#define SEEK_END        2

#define _IOFBF          0x000    
#define _IOREAD         0x001  
#define _IOWRITE        0x002
#define _IONBF          0x004  
#define _IOMYBUF        0x008  
#define _IOEOF          0x010  
#define _IOERR          0x020
#define _IOLBF          0x040
#define _IOREADING      0x080
#define _IOWRITING      0x100
#define _IOAPPEND       0x200

#define BUFSIZ          1024
#ifndef NULL
#define NULL            ((void *)0)
#endif
#define EOF             (-1)

extern FILE     __stdin, __stdout, __stderr;

extern FILE     *iotab[FOPEN_MAX];

#define fileno(fp)	((fp)->fd)

#define getchar()       getc(stdin)
#define putchar(c)      putc(c, stdout)
#define getc(p)         (--(p)->count >= 0 ? (int) (*(p)->ptr++) : \
                                __fillbuf(p))
#define putc(c, p)      ((--(p)->count >= 0 && c != '\n') ? \
                         (int) (*(p)->ptr++ = (c)) : \
                         __flushbuf((c),(p)))

#define feof(p)         (((p)->flags & _IOEOF) != 0)
#define ferror(p)       (((p)->flags & _IOERR) != 0)
#define clearerr(p)     ((p)->flags &= ~(_IOERR|_IOEOF))

int printf(const char *fmt, ...);
int fprintf(FILE *stream, const char *fmt, ...);
int sprintf(char *str, const char *fmt, ...);
int snprintf(char *str, size_t size, const char *fmt, ...);
int vsprintf(char *str, const char *fmt, va_list ap);
int vfprintf(FILE *stream, const char *fmt, va_list ap);
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap);
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
int fflush(FILE *stream);
char *fgets(char *s, int size, FILE *stream);
int fputs(const char *s, FILE *stream);
int puts(const char *s);

#endif

