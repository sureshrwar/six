/*
 * int0x80() -- enter the SIX kernel.
 *
 * SIX has no CPU and therefore no trap instruction.  A guest enters the
 * kernel by sending the host process a signal, which SIX's sun_handler()
 * catches and routes to system_call().  The name is historical: on x86
 * this was once a literal call gate, and on SPARC it never was.
 *
 * On x86 this is now only reachable from code that does not need to pass
 * arguments.  Library/sys/syscall.c issues its own trap inline, because
 * it has to keep %esi -- which points at the argument block -- live right
 * up to the trapping instruction, and it cannot do that across a function
 * call.  See include/asm-six/sixcall.h.
 */

#include <sys/sixcall.h>

void int0x80()
{
#if (__i386__)
	/*
	 * The 2005 version went through the Solaris call gate:
	 *
	 *      pushl $0x21             ! SIGLWP, arguments on the stack
	 *      movl  $0x14,%eax        ! getpid
	 *      lcall $0x7,$0x0
	 *      pushl %eax              ! the pid it returned
	 *      pushl $0x5              ! dummy
	 *      movl  $0x25,%eax        ! kill
	 *      lcall $0x7,$0x0
	 *
	 * Linux/i386 uses "int $0x80" with the arguments in registers
	 * instead.  The call numbers are unchanged -- Solaris and Linux
	 * both inherit them from System V -- so only the gate and the
	 * argument passing differ:
	 *
	 *      getpid = 20 = 0x14      kill = 37 = 0x25
	 *
	 * The signal number did change.  It was SIGLWP (0x21 = 33), which
	 * on Linux is reserved by NPTL and cannot be caught; see
	 * arch/six/kernel/host.h for why the trap is SIGRTMIN+4 now.
	 *
	 * (The original was also written as a GCC-2.x multi-line string
	 * literal, which no compiler since has accepted.)
	 */
	int pid;

	__asm__ __volatile__ ("int $0x80"
			      : "=a" (pid)
			      : "0"  (20)               /* __NR_getpid */
			      : "memory");

	__asm__ __volatile__ ("int $0x80"
			      :
			      : "a" (37),               /* __NR_kill    */
				"b" (pid),
				"c" (SIX_TRAPSIG)
			      : "memory");
#else
	int pid;
	pid = sparc_sys(20, 0, 0, 0);	/* get our pid */
	sparc_sys(37, pid, 33, 0);	/* hit us with a SIGLWP */
#endif
}
