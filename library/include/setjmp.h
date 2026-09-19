
#ifndef _LIBRARY_SETJMP_H
#define _LIBRARY_SETJMP_H


#if (__i386__)
typedef int	jmp_buf[128];	
#else
typedef int	jmp_buf[112];	
#endif

extern int setjmp(jmp_buf env);
extern void longjmp(jmp_buf env, int result);

#endif
 
