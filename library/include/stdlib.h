#ifndef _LIBRARY_STDLIB_H
#define _LIBRARY_STDLIB_H

/*
 * <stdlib.h> for guest programs.
 *
 * This file used to declare optarg and optind and nothing else.  Everything
 * a guest program actually called -- exit(), malloc(), getenv() -- it called
 * with no declaration in scope, which C89 permits and which works only for
 * as long as every such function returns int.  It does not: malloc() returns
 * a pointer, and on a machine where that is the same width as an int the
 * mistake is invisible.  GCC 15 rejects it outright, which is how this came
 * to light.
 *
 * Everything declared here is actually implemented somewhere in
 * library/libc or library/sys.  Nothing is declared speculatively: if a
 * guest program calls something that is not in this file, the function does
 * not exist and the link will fail, which is the correct outcome.
 */

#include <linux/types.h>

#ifndef NULL
#define NULL	((void *) 0)
#endif

#define EXIT_SUCCESS	0
#define EXIT_FAILURE	1

/* --- process termination -------------------------------------------------
 * exit() is library/sys/exit.c and runs the stdio cleanup; _exit() is the
 * raw system call.
 */
extern void exit(int status);
extern void _exit(int status);
extern void abort(void);

/* --- memory --------------------------------------------------------------
 * library/libc/malloc.c, a first-fit allocator over sbrk().
 */
extern void *malloc(unsigned int size);
extern void *calloc(unsigned int nmemb, unsigned int size);
extern void *realloc(void *ptr, unsigned int size);
extern void free(void *ptr);
extern char *sbrk(int increment);
extern int brk(void *end_data_segment);

/* --- conversion ---------------------------------------------------------- */
extern int atoi(const char *s);
extern long strtol(const char *s, char **endptr, int base);
extern unsigned long strtoul(const char *s, char **endptr, int base);

/* --- environment ---------------------------------------------------------
 * There is no putenv() or setenv(); the guest's environment is whatever
 * execve() was handed.  envlist is the environment itself, set up by
 * cstart.
 */
extern char *getenv(const char *name);
extern char **envlist;

/* --- getopt --------------------------------------------------------------
 * library/libc/getopt.c.  optopt is declared but, unlike the other three,
 * is not defined anywhere in this libc -- referencing it will not link.
 */
extern int getopt(int argc, char *const argv[], const char *optstring);
extern char *optarg;
extern int optind, opterr, optopt;

#endif /* _LIBRARY_STDLIB_H */
