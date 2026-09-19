/*
 * Syscall() -- the guest side of the SIX system call interface.
 *
 * Everything in library/sys/ is a thin stub that marshals its arguments and
 * lands here.  This function is the one place where a guest program leaves
 * its own address space and enters the kernel.
 *
 * ---------------------------------------------------------------------------
 * How it used to work on x86, and why that cannot survive on Linux
 * ---------------------------------------------------------------------------
 *
 * The 2005 code packed the arguments into the MMX registers:
 *
 *      mm0 = (arg1 << 32) | syscall_number
 *      mm2 = (arg2 << 32) | arg3
 *
 * And trapped.  MMX aliases the x87 register stack, Solaris embedded the
 * whole FPU save area inside the ucontext_t it handed to the signal
 * handler, and so the kernel could read the arguments straight out of the
 * signal frame at fixed word offsets.  That is what the g2..g9 fields of
 * struct pt_regs originally were.
 *
 * It was clever and it is unrecoverable on Linux, for two independent
 * reasons.  Linux calls fpu__clear_user_states() on the way into a signal
 * handler, so mm0 reads back as zero -- measured on this host, not assumed.
 * And glibc keeps the FPU state out of line behind a pointer, so there is
 * no fixed offset to alias even if the state survived.
 *
 * ---------------------------------------------------------------------------
 * What it does now
 * ---------------------------------------------------------------------------
 *
 * The arguments go in an ordinary struct on the guest's own stack, and the
 * guest hands over its *address* in %esi.
 *
 * %esi works where MMX does not because it is an ordinary general-purpose
 * register: Linux saves it in uc_mcontext.gregs like any other, and a
 * system call clobbers only %eax.  So the value is still there when
 * system_call() looks at regs->esi.
 *
 * A pointer also sidesteps the argument-count problem entirely.  One
 * register carries any number of arguments, which matters for the
 * six-argument calls -- mmap in particular still uses the 2005 two-trap
 * protocol precisely because only three arguments would fit.
 *
 * The kernel can dereference a guest pointer directly because in SIX guest
 * virtual addresses *are* host virtual addresses: set_proc_mappings() maps
 * each task's pages at the guest addresses themselves on every context
 * switch.  system_call() still bounds-checks the pointer against TASK_SIZE
 * before touching it.
 *
 * ---------------------------------------------------------------------------
 * The trap itself
 * ---------------------------------------------------------------------------
 *
 * There is no trap instruction, because there is no CPU to trap to.  A
 * guest enters the kernel the same way the kernel enters itself: by sending
 * the host process a signal, which SIX's sun_handler() catches.
 *
 * Note that "int $0x80" below is a real Linux system call, not SIX's.  It
 * is getpid() and then kill(), issued as raw assembly rather than through a
 * libc -- the guest has no libc beyond this one, and in any case going
 * through a function call would put an unpredictable instruction between
 * setting %esi and the trap.
 *
 * The signal is delivered on return from kill(), so the context the handler
 * sees is the instruction after the trap, with every register except %eax
 * holding what it held before.  That is the whole trick: %esi is still
 * pointing at the argument block.
 */

#include <sys/sixcall.h>

int syscall(int num, long one, long two, long three)
{
#if (__i386__)
	struct six_guest_call args;
	int pid;

	args.nr = (unsigned long) num;
	args.a1 = (unsigned long) one;
	args.a2 = (unsigned long) two;
	args.a3 = (unsigned long) three;
	args.a4 = 0;
	args.a5 = 0;
	args.a6 = 0;
	args.ret = 0;

	/*
	 * Host getpid().  Solaris and Linux agree on the number -- both
	 * inherit it from System V -- so only the gate differs from the
	 * lcall $7,$0 the 2005 code used.
	 */
	__asm__ __volatile__ ("int $0x80"
			      : "=a" (pid)
			      : "0"  (20)               /* __NR_getpid */
			      : "memory");

	/*
	 * Host kill(pid, SIX_TRAPSIG).  &args is fed in through "S" so that
	 * %esi is live across the trap; the compiler cannot insert anything
	 * between loading it and executing the instruction, because from
	 * its point of view the asm is a single indivisible operation.
	 *
	 * "b" is safe only because the guest is built -fno-pic; with PIC,
	 * %ebx is the GOT pointer and GCC will refuse the constraint.
	 */
	__asm__ __volatile__ ("int $0x80"
			      :
			      : "a" (37),               /* __NR_kill    */
				"b" (pid),
				"c" (SIX_TRAPSIG),
				"S" (&args)
			      : "memory");

	/* system_call() wrote the result back into the block. */
	return (int) args.ret;
#else
	long ret;

	/*
	 * SPARC needs none of this.  %g2..%g5 are real registers, they are
	 * saved into the ucontext by the host on signal delivery, and this
	 * is the path that actually worked in 2005.  Left exactly as it
	 * was.
	 *
	 * The long commentary that used to live here, walking through all
	 * seven stages from the stub to put_ret() and back, has moved to
	 * include/asm-six/sixcall.h so that both halves of the protocol are
	 * described in one place.
	 */
	__asm__("mov %1, %%g2"   :  "=r" (ret)  : "r" (num));
	__asm__("mov %1, %%g3"   :  "=r" (ret)  : "r" (one));
	__asm__("mov %1, %%g4"   :  "=r" (ret)  : "r" (two));
	__asm__("mov %1, %%g5"   :  "=r" (ret)  : "r" (three));
	__asm__("mov 0, %g6");

	int0x80();

	__asm__("mov %%g2, %0" : "=r" (ret));

	return ret;
#endif
}
