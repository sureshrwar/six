#include <stdio.h>
#include <linux/fcntl.h>

FILE *
fopen(const char *name, const char *mode)
{
        register int i;
        int rwmode = 0, rwflags = 0;
        FILE *stream;
        int fd, flags = 0;

        for (i = 0; iotab[i] != 0 ; i++)
                if (i >= FOPEN_MAX-1)
                        return (FILE *)NULL;

        switch(*mode++) {
        case 'r':
                flags |= _IOREAD | _IOREADING;
                rwmode = O_RDONLY;
                break;
        case 'w':
                flags |= _IOWRITE | _IOWRITING;
                rwmode = O_WRONLY;
                rwflags = O_CREAT | O_TRUNC;
                break;
        case 'a':
                flags |= _IOWRITE | _IOWRITING | _IOAPPEND;
                rwmode = O_WRONLY;
                rwflags |= O_APPEND | O_CREAT;
                break;
        default:
                return (FILE *)NULL;
        }

        while (*mode) {
                switch(*mode++) {
                case 'b':
                        continue;
                case '+':
                        rwmode = O_RDWR;
                        flags |= _IOREAD | _IOWRITE;
                        continue;
                /* The sequence may be followed by additional characters */
                default:
                        break;
                }
                break;
        }

        /* Perform a creat() when the file should be truncated or when
         * the file is opened for writing and the open() failed.
         */
        if ((rwflags & O_TRUNC)
            || (((fd = open(name, rwmode)) < 0)
                    && (rwflags & O_CREAT))) {
                if (((fd = creat(name, 0666)) > 0) && flags  | _IOREAD) {
                        (void) close(fd);
                        fd = open(name, rwmode);
                }

        }

        if (fd < 0) return (FILE *)NULL;

        if (( stream = (FILE *) malloc(sizeof(FILE))) == NULL ) {
                close(fd);
                return (FILE *)NULL;
        }

        if ((flags & (_IOREAD | _IOWRITE))  == (_IOREAD | _IOWRITE))
                flags &= ~(_IOREADING | _IOWRITING);

        stream->count = 0;
        stream->fd = fd;
        stream->flags = flags;
        stream->buf = NULL;
        stream->ptr = NULL;
        iotab[i] = stream;
        return stream; 
}       

