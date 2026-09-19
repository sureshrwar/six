
#ifndef _LINUX_LIMITS_H
#define _LINUX_LIMITS_H

#define NR_OPEN          256

#define NGROUPS_MAX       32    /* supplemental group IDs are available */
#define ARG_MAX       131072    /* # bytes of args + environ for exec() */
#define CHILD_MAX        999    /* no limit :-) */
#define OPEN_MAX         256    /* # open files a process may have */
#define LINK_MAX         127    /* # links a file may have */
#define MAX_CANON        255    /* size of the canonical input queue */
#define MAX_INPUT        255    /* size of the type-ahead buffer */
#define NAME_MAX         255    /* # chars in a file name */
#define PATH_MAX        1024    /* # chars in a path name */
#define PIPE_BUF        4096    /* # bytes in atomic write to a pipe */

#if (SIX)

#define CHAR_BIT           8    /* # bits in a char */
#define CHAR_MIN        -128    /* minimum value of a char */
#define CHAR_MAX         127    /* maximum value of a char */
#define SCHAR_MIN       -128    /* minimum value of a signed char */ 
#define SCHAR_MAX        127    /* maximum value of a signed char */
#define UCHAR_MAX        255    /* maximum value of an unsigned char */
#define MB_LEN_MAX         1    /* maximum length of a multibyte char */

#define SHRT_MIN  (-32767-1)    /* minimum value of a short */
#define SHRT_MAX       32767    /* maximum value of a short */
#define USHRT_MAX     0xFFFF    /* maximum value of unsigned short */

#define INT_MIN (-2147483647-1) /* minimum value of a 32-bit int */
#define INT_MAX         ((int)(~0U>>1))
#define UINT_MAX        (~0U)   

#define LONG_MIN (-2147483647L-1)/* minimum value of a long */ 
#define LONG_MAX  2147483647L   /* maximum value of a long */
#define ULONG_MAX 0xFFFFFFFFL   /* maximum value of an unsigned long */

/* Minimum sizes required by the POSIX P1003.1 standard (Table 2-3). */
#ifdef _POSIX_SOURCE            /* these are only visible for POSIX */
#define _POSIX_ARG_MAX    4096  /* exec() may have 4K worth of args */
#define _POSIX_CHILD_MAX     6  /* a process may have 6 children */
#define _POSIX_LINK_MAX      8  /* a file may have 8 links */
#define _POSIX_MAX_CANON   255  /* size of the canonical input queue */
#define _POSIX_MAX_INPUT   255  /* you can type 255 chars ahead */
#define _POSIX_NAME_MAX     14  /* a file name may have 14 chars */
#define _POSIX_NGROUPS_MAX   0  /* supplementary group IDs are optional */
#define _POSIX_OPEN_MAX     16  /* a process may have 16 files open */
#define _POSIX_PATH_MAX    255  /* a pathname may contain 255 chars */
#define _POSIX_PIPE_BUF    512  /* pipes writes of 512 bytes must be atomic */
#define _POSIX_STREAM_MAX    8  /* at least 8 FILEs can be open at once */
#define _POSIX_TZNAME_MAX    3  /* time zone names can be at least 3 chars */
#define _POSIX_SSIZE_MAX 32767  /* read() must support 32767 byte reads */

/* Some of these old names had better be defined when not POSIX. */
#define _NO_LIMIT        100    /* arbitrary number; limit not enforced */

#define STREAM_MAX        20    /* must be the same as FOPEN_MAX in stdio.h */
#define TZNAME_MAX         3    /* maximum bytes in a time zone name is 3 */
#define SSIZE_MAX      32767    /* max defined byte count for read() */

#endif //SIX
#endif
#endif

