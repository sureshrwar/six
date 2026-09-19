/*
 * setjmp_i386.S -- setjmp/longjmp for the x86 guest.
 *
 * The SPARC port has library/libc/setjmp.s; there has never been an x86
 * equivalent, which is one of the reasons the real Bourne shell was never
 * built on x86.  sh(1) cannot work without this: it uses setjmp/longjmp for
 * every error unwind, for break and continue inside loops, and for the
 * parser's failure path (see failpt/errpt in applications/sh/sh.c).
 *
 * Minix semantics, the same contract setjmp.s implements on SPARC:
 *
 *     setjmp/longjmp     save and restore the signal mask
 *     _setjmp/_longjmp   do not
 *
 * jmp_buf layout (library/include/setjmp.h declares int[128], so there is
 * room to spare; SPARC uses the same first two slots):
 *
 *     env[0]   0   flags -- SJ_SIGMASK if the mask in env[1] is valid
 *     env[1]   4   saved signal mask
 *     env[2]   8   %ebx
 *     env[3]  12   %esi
 *     env[4]  16   %edi
 *     env[5]  20   %ebp
 *     env[6]  24   %esp, as it will be *after* setjmp returns
 *     env[7]  28   return address
 *
 * Only the callee-saved registers are kept.  That is all longjmp is required
 * to restore: anything in a caller-saved register at the time of the longjmp
 * is indeterminate afterwards, which is exactly why the shell marks the
 * variables it needs across a setjmp as volatile.
 *
 * The mask is read with sgetmask()/ssetmask() rather than sigprocmask().
 * That is not laziness: library/sys/sigprocmask.c returns -EINVAL when the
 * new-mask argument is NULL, so it cannot be used to *query* the mask at
 * all.  sgetmask/ssetmask are the old Linux 2.0 calls that do exactly what
 * is needed in one trap each, and both are already wired into
 * sys_call_table (six_sgetmask/six_ssetmask in arch/six/kernel/irq.c).
 *
 * Caveat inherited from library/sys/ssetmask.c: it rejects a negative mask,
 * so a mask with bit 31 set (signal 32) cannot be restored.  SIX only ever
 * uses signals 1..31 in the guest, so this never bites in practice.
 */

#define SJ_SIGMASK	2		/* env[0] when the mask was saved   */

	.text

/* ------------------------------------------------------------------ */
/* int setjmp(jmp_buf env) -- saves the signal mask                    */
/* ------------------------------------------------------------------ */
	.globl	setjmp
	.type	setjmp, @function
setjmp:
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%ebx			/* callee-saved; we need a scratch  */
	movl	8(%ebp), %ebx		/* %ebx = env                       */

	call	sgetmask
	movl	%eax, 4(%ebx)		/* env[1] = current signal mask     */
	movl	$SJ_SIGMASK, (%ebx)	/* env[0] = "mask is valid"         */

	movl	%ebx, %eax		/* carry env past the epilogue      */
	popl	%ebx			/* put the caller's %ebx back ...   */
	popl	%ebp			/* ... and %ebp, and unwind %esp    */
	jmp	.Lsave			/* now the machine state is exactly */
					/* as it was on entry to setjmp     */
	.size	setjmp, .-setjmp

/* ------------------------------------------------------------------ */
/* int _setjmp(jmp_buf env) -- does not touch the signal mask          */
/* ------------------------------------------------------------------ */
	.globl	_setjmp
	.type	_setjmp, @function
_setjmp:
	movl	4(%esp), %eax		/* %eax = env                       */
	movl	$0, (%eax)		/* env[0] = "no mask saved"         */
	/* fall through */
	.size	_setjmp, .-_setjmp

/*
 * Common tail.  Entered with %eax = env, and with every register and %esp
 * holding precisely what they held when the caller executed "call setjmp",
 * so that (%esp) is the return address and 4(%esp) is the caller's stack
 * pointer as it will be once we return.
 */
	.type	.Lsave, @function
.Lsave:
	movl	%ebx,  8(%eax)
	movl	%esi, 12(%eax)
	movl	%edi, 16(%eax)
	movl	%ebp, 20(%eax)
	leal	4(%esp), %ecx		/* %esp after the ret pops          */
	movl	%ecx, 24(%eax)
	movl	(%esp), %ecx		/* return address                   */
	movl	%ecx, 28(%eax)
	xorl	%eax, %eax		/* the direct call returns 0        */
	ret

/* ------------------------------------------------------------------ */
/* void longjmp(jmp_buf env, int val)                                  */
/* ------------------------------------------------------------------ */
	.globl	longjmp
	.type	longjmp, @function
longjmp:
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%ebx
	movl	8(%ebp), %ebx		/* %ebx = env                       */

	cmpl	$SJ_SIGMASK, (%ebx)	/* was a mask saved?                */
	jne	.Lnomask
	pushl	4(%ebx)
	call	ssetmask
	addl	$4, %esp
	jmp	.Lnomask
	.size	longjmp, .-longjmp

/* ------------------------------------------------------------------ */
/* void _longjmp(jmp_buf env, int val) -- leaves the signal mask alone */
/* ------------------------------------------------------------------ */
	.globl	_longjmp
	.type	_longjmp, @function
_longjmp:
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%ebx
	movl	8(%ebp), %ebx
	/* fall through */
	.size	_longjmp, .-_longjmp

	.type	.Lnomask, @function
.Lnomask:
	movl	12(%ebp), %eax		/* val -- read before %ebp dies     */
	testl	%eax, %eax
	jnz	.Lgo
	incl	%eax			/* longjmp(env,0) must return 1     */
.Lgo:
	/*
	 * Load everything out of env *before* switching stacks.  env is
	 * usually a global or a struct member, but it is allowed to live on
	 * the frame we are about to abandon, and reading it after %esp has
	 * moved would leave it exposed to anything the host pushes below the
	 * new stack pointer.
	 */
	movl	12(%ebx), %esi
	movl	16(%ebx), %edi
	movl	20(%ebx), %ebp
	movl	24(%ebx), %edx		/* target %esp                      */
	movl	28(%ebx), %ecx		/* target %eip                      */
	movl	 8(%ebx), %ebx		/* target %ebx -- kills our pointer */

	movl	%edx, %esp
	jmp	*%ecx			/* reappear as a nonzero setjmp()   */

/*
 * The guest has no runtime that needs an executable stack, and the SIX ELF
 * loader ignores PT_GNU_STACK anyway, but say so explicitly: without this
 * note GNU ld marks the whole program's stack executable the moment a
 * single assembly object appears in the link.
 */
	.section .note.GNU-stack,"",@progbits
