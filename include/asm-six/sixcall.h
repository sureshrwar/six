
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
 * and the handler read the values back out of the *saved FPU state that
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
 * library/sys/syscall.c uses the same MMX trick from the other side, and
 * is equally broken on Linux.  Guest userland is not built yet (it is
 * still linked with Solaris ld mapfiles), so that half of the protocol is
 * deliberately left for later.  When it is revived, the guest cannot
 * simply reference six_call by name -- it is separately linked -- so it
 * will need either a fixed address inside the emulated RAM (the zero page
 * is already used to pass the command line) or a switch to general-purpose
 * registers, which unlike MMX are preserved in the signal frame.
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

#endif /* __i386__ */

#endif /* _SIX_ASM_SIXCALL_H */
