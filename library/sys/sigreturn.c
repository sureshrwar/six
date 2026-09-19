
#include <syscall.h>
#include <linux/errno.h>
#include <asm/sigcontext.h>


int sigreturn()
{
#if (__i386__)
	int ret;
	long long val;
	long *valp;
#endif
	int (*f)();
	struct sigcontext *sc;
#if (!__i386__)
	/*
	 * g6 contains an address from our stack, where the original
	 * context is saved. we need it so that we can pass it over
	 * to the sigreturn system call, which in turn will restore
	 * that context and bring us back to what we were doing when
	 * the signal intruded.
	 */
	__asm__("mov %%g6, %0" : "=r" (sc));
	/*
	 * kernel routines will ensure that g7 contains the address
	 * of the original handler function specified by the process.
	 * so get it and call it.
	 */
	__asm__("mov %%g7, %0" : "=r" (f));
#else
	valp = &val;
	__asm__("movq %%mm4, (%1)" : "=r" (ret) : "r" (valp));
	sc = (struct sigcontext *)val;
	val >>= 32;
	f = val;	
#endif
	/*
	 * call the original signal handler
	 */
	f();
	/*
	 * time to mop up!
	 */
        return syscall(__NR_sigreturn, (long)sc, 0, 0);
}

