
#ifndef _SIX_ASM_SIXCALL_H
#define _SIX_ASM_SIXCALL_H

/*
 * The SIX syscall argument channel.
 *
 * WHAT THIS REPLACES
 *
 * SIX has no CPU, so it has no trap instruction and no register file to
 * put syscall arguments in.  The 2005 x86 port solved that by using the
 * MMX registers as a side channel:
 *
 *      mm0 = (arg1 << 32) | syscall_number
 *      mm2 = (arg2 << 32) | arg3
 *      mm4 = flags
 *      raise(SIGLWP);                  <- "trap"
 *
 * And the handler read the values back out of the *saved FPU state that
 * Solaris embeds in the ucontext_t*, which is why struct pt_regs had
 * fields called g2..g9 sitting at word offsets 35, 36, 40, 41, ... : those
 * are where mm0, mm2, mm4 and mm6 land in an x87 FSAVE image.  (Only even
 * MMX registers were used because the 10-byte-per-ST-register stride only
 * comes out word-aligned every other register.)
 *
 * It was a genuinely clever trick, and it is completely unportable to
 * Linux, for two independent reasons:
 *
 *   1. Linux *zeroes* the FPU state before entering a signal handler.  The
 *      kernel calls fpu__clear_user_states() on the way in, so by the time
 *      the handler runs, mm0 reads back as 0.  Confirmed experimentally on
 *      this host: write 0x1122334455667788 to mm0, raise(SIGRTMIN+4), and
 *      the handler sees 0x0000000000000000.
 *
 *   2. glibc does not embed the FPU state in the ucontext at all.  It
 *      lives out of line behind a uc_mcontext.fpregs pointer, so there is
 *      no fixed word offset to alias even if (1) were false.
 *
 * WHAT IT DOES INSTEAD
 *
 * Plain memory.  The channel below is an ordinary struct.  SIX is a single
 * host process, so a caller can fill it in, raise the trap, and have
 * system_call() read it -- with no dependence on what the host does to the
 * machine state across signal delivery, which is the whole problem.
 *
 * The register *names* are kept (g2..g7) and system_call() copies the
 * channel into the incoming pt_regs before dispatching, so all 165 entries
 * of sys_call_table and every grab_args()/put_ret() pair carry on working
 * exactly as written.  The calling convention is unchanged too:
 *
 *      g2 = syscall number      (and, on the way back, the return value)
 *      g3 = arg1                (was mm0 high half)
 *      g4 = arg3                (was mm2 low half)   <-- note the order
 *      g5 = arg2                (was mm2 high half)
 *      g6 = flags               (was mm4)
 *
 * The g4/g5 inversion is not a mistake; it falls out of how the 2005 code
 * packed mm2, and grab_args() has always compensated for it.  Preserved
 * verbatim so the syscall thunks did not have to be touched.
 *
 * SPARC is left alone: %g2..%g5 are real registers there, they survive
 * signal delivery, and that path was the one that actually worked in 2005.
 *
 * GUEST PROGRAMS
 *
 * Guest programs cannot use the channel above.  They are separately linked
 * ELF executables living in the emulated RAM; they have no way to resolve
 * the symbol "six_call", and even if they did, the address they would need
 * is a host address rather than a guest one.
 *
 * So the guest passes a *pointer* instead, in %esi:
 *
 *      struct six_guest_call args;     <- on the guest's own stack
 *      args.nr   = __NR_write;
 *      args.a1   = fd;  args.a2 = buf;  args.a3 = count;
 *      asm("int $0x80" : : "S" (&args)); <- "S" is the %esi constraint
 *
 * And system_call() picks it up out of the signal frame with regs->esi.
 *
 * Why %esi, and why this works:
 *
 *   - Linux clobbers only %eax across "int $0x80".  Every other
 *     general-purpose register arrives in the signal frame with the value
 *     it had at the trap instruction.  (This is the opposite of the MMX
 *     situation described above, where the host deliberately wipes the
 *     state we wanted to read.)
 *   - %esi is a general-purpose register, so it is saved in
 *     uc_mcontext.gregs like any other -- no FPU save area, no layout
 *     archaeology.
 *   - A pointer is one register wide no matter how many arguments a call
 *     takes, so the six-argument calls (mmap, select) need no special case.
 *
 * The alternative considered and rejected was a fixed well-known address
 * in the guest address space, on the model of the command line that
 * already sits in the zero page.  It would have baked a second magic
 * address into both halves of the system, and it would have been
 * per-process state at a per-machine address, which is wrong the moment
 * two guests run at once.
 *
 * Note that the kernel's own internal callers (kernel_thread(), and the
 * exit path in kernel_thread_start()) cannot use %esi: they reach the trap
 * through raise(), which is a libc call, and %esi is callee-saved, so its
 * value at the inner "int $0x80" is not something we get to choose.  Hence
 * the two paths.  system_call() distinguishes them with user_mode(regs).
 */

#if (__i386__)

struct six_call_regs {
	unsigned long g2;       /* syscall number, then return value */
	unsigned long g3;       /* arg1                              */
	unsigned long g4;       /* arg3                              */
	unsigned long g5;       /* arg2                              */
	unsigned long g6;       /* flags                             */
	unsigned long g7;
};

/* Defined in arch/six/kernel/irq.c. */
extern struct six_call_regs six_call;

/*
 * The guest-side argument block, pointed to by %esi.
 *
 * Unlike six_call_regs above, this one is in natural argument order.  There
 * is no reason to inherit the g4/g5 inversion here -- that was an artefact
 * of how the 2005 code packed the mm2 register, and nothing outside the
 * kernel ever saw it.  system_call() does the reordering when it copies
 * this block into pt_regs, so the syscall thunks still see what they have
 * always seen.
 *
 * This structure is ABI between separately-built halves of the system:
 * the kernel (built for the host) and the guest libc (built for the
 * emulated machine).  Both are 32-bit x86 with the same alignment rules,
 * so a plain struct of unsigned longs is safe, but it must not be
 * reordered on one side only.  Library/include/sys/sixcall.h carries an
 * identical copy for the guest.
 */
struct six_guest_call {
	unsigned long nr;       /* system call number                */
	unsigned long a1;
	unsigned long a2;
	unsigned long a3;
	unsigned long a4;
	unsigned long a5;
	unsigned long a6;
	unsigned long ret;      /* written back by system_call()     */
};

#endif /* __i386__ */

#endif /* _SIX_ASM_SIXCALL_H */
