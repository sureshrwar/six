
#include <syscall.h>
#include <linux/errno.h>
#include <asm/sigcontext.h>

/*
 * The signal trampoline.
 *
 * The kernel never jumps straight at a guest's signal handler.  It jumps
 * here instead, having first pushed a copy of the interrupted context
 * (a struct sigcontext) onto the guest's own stack.  We call the real
 * handler and then ask the kernel to put the old context back.
 *
 * The address of this function is handed to the kernel by sigaction(),
 * which is why sigaction() traps twice -- see library/sys/sigaction.c and
 * six_sigaction() in arch/six/kernel/irq.c.  The kernel keeps it in
 * current->_sigreturn.
 *
 * How the two values reach us is architecture specific:
 *
 *   sparc  in the global registers %g6 (sigcontext) and %g7 (handler),
 *          which survive the trip because SPARC has globals to spare.
 *
 *   x86    as ordinary cdecl arguments.  There are no spare registers,
 *          and the obvious candidates do not survive: the MMX channel the
 *          2005 code used here (movq %mm4) is dead, because Linux zeroes
 *          the FPU state before entering a signal handler, so %mm4 always
 *          read back as 0.  setup_frame() in arch/six/kernel/signal.c
 *          hand-builds a call frame for us on the guest stack instead.
 *
 * This function does not return.  The sigreturn system call rewrites the
 * context that the kernel is about to setcontext() to, so control resumes
 * wherever the signal interrupted -- not here.
 */
#if (__i386__)

int sigreturn(struct sigcontext *sc, int (*f)())
{
	/*
	 * call the original signal handler
	 */
	f();
	/*
	 * time to mop up!
	 */
	return syscall(__NR_sigreturn, (long)sc, 0, 0);
}

#else

int sigreturn()
{
	int (*f)();
	struct sigcontext *sc;

	/*
	 * g6 contains an address from our stack, where the original
	 * context is saved. We need it so that we can pass it over
	 * to the sigreturn system call, which in turn will restore
	 * that context and bring us back to what we were doing when
	 * the signal intruded.
	 */
	__asm__("mov %%g6, %0" : "=r" (sc));
	/*
	 * Kernel routines will ensure that g7 contains the address
	 * of the original handler function specified by the process.
	 * So get it and call it.
	 */
	__asm__("mov %%g7, %0" : "=r" (f));
	/*
	 * call the original signal handler
	 */
	f();
	/*
	 * time to mop up!
	 */
	return syscall(__NR_sigreturn, (long)sc, 0, 0);
}

#endif
