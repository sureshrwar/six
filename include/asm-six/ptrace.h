
#ifndef _SIX_ASM_PTRACE_H
#define _SIX_ASM_PTRACE_H

/*
 * struct pt_regs is not a description of a machine's registers.
 *
 * It is a byte-exact overlay of the host's ucontext_t.  SIX never gets to
 * see a real trap frame -- the "CPU" is a host process and the only thing
 * an interrupt hands it is a ucontext_t from a signal handler -- so the
 * kernel simply names the fields it cares about and pads over the rest.
 * Every regs->pc and regs->esp in the tree is really an index into a saved
 * host context.
 *
 * The 2005 layout below (kept in the comments) was counted against the
 * *Solaris* ucontext_t.  glibc's differs in almost every position, so the
 * padding has been recounted.  Do not adjust these numbers by hand: run
 *
 *      gcc -m32 -o /tmp/uclayout port/tools/ucontext_layout.c && /tmp/uclayout
 *
 * and transcribe what it prints.  The build will refuse to compile if the
 * total size stops matching sizeof(ucontext_t) -- see the assertion at the
 * bottom of this file.
 *
 *      field         Solaris word   glibc word
 *      uc_sp              6              2
 *      uc_sp_size         7              4
 *      ebp               15             11
 *      pc                23             19
 *      psw               25             21
 *      esp               26             22
 *      sizeof           512            364
 *
 * The g2..g9 "registers" moved much further than that.  On Solaris they
 * were not padding-separated fields at all: they landed *inside the FPU
 * save area embedded in the ucontext*, at words 35/36, 40/41, 45/46 and
 * 50/51.  That was deliberate.  SIX passes syscall arguments in MMX
 * registers, MMX aliases the x87 stack, and an x87 FSAVE image stores each
 * ST register in 10 bytes -- so mm0 lands at +0, mm2 at +20, mm4 at +40,
 * mm6 at +60.  A 20-byte stride is 5 words, which is exactly the spacing
 * above, and it is why only the *even* MMX registers were ever used: they
 * are the ones that come out word-aligned.
 *
 * None of that can survive on Linux, for two independent reasons:
 *
 *   1. Linux zeroes the FPU (and therefore MMX) state before entering a
 *      signal handler -- see fpu__clear_user_states() in the kernel's
 *      signal delivery path.  Verified experimentally: a value written to
 *      mm0 immediately before raise() reads back as 0 inside the handler.
 *
 *   2. glibc does not embed the FPU state in the ucontext.  It keeps it
 *      out of line behind a uc_mcontext.fpregs *pointer*, so there is no
 *      fixed word offset to alias in the first place.
 *
 * So g2..g9 are now ordinary storage appended *after* the host context.
 * swapcontext(), makecontext() and setcontext() only ever touch the first
 * sizeof(ucontext_t) bytes, so the extra fields ride along untouched
 * through context switches, and every existing regs->g2 in the syscall
 * layer keeps working unchanged.  system_call() is what now loads them --
 * see arch/six/kernel/irq.c and include/asm-six/sixcall.h.
 */

#if (__i386__)

/*
 * glibc's greg indices, repeated here so that kernel code does not have to
 * pull in <sys/ucontext.h> (which would drag in the host's signal
 * definitions and collide with the Solaris ones in asm-six/signal.h).
 *
 * These are indices into uc_mcontext.gregs, which begins at word 5.
 */
#define GS              0
#define FS              1
#define ES              2
#define DS              3
#define EDI             4
#define ESI             5
#define EBP             6
#define ESP             7
#define EBX             8
#define EDX             9
#define ECX             10
#define EAX             11
#define TRAPNO          12
#define ERR             13
#define EIP             14
#define CS              15
#define EFL             16
#define UESP            17
#define SS              18

/* Where uc_mcontext.gregs starts, in words. */
#define SIX_GREGS_WORD  5

#else

#define REG_PSR (0)
#define REG_PC  (1)
#define REG_nPC (2)
#define REG_Y   (3)
#define REG_G1  (4)
#define REG_G2  (5)
#define REG_G3  (6)
#define REG_G4  (7)
#define REG_G5  (8)
#define REG_G6  (9)
#define REG_G7  (10)
#define REG_O0  (11)
#define REG_O1  (12)
#define REG_O2  (13)
#define REG_O3  (14)
#define REG_O4  (15)
#define REG_O5  (16)
#define REG_O6  (17)
#define REG_O7  (18)

/* the following defines are for portability */
#define REG_PS  REG_PSR
#define REG_SP  REG_O6
#define REG_R0  REG_O0
#define REG_R1  REG_O1

#endif


struct pt_regs {
#if (__i386__)
        /* --- host ucontext_t: 91 words / 364 bytes --------------------- */
        unsigned int uc_flags;          /*  0                             */
        unsigned int uc_link;           /*  1                             */
        unsigned int uc_sp;             /*  2  uc_stack.ss_sp             */
        unsigned int uc_sp_flags;       /*  3  uc_stack.ss_flags          */
        unsigned int uc_sp_size;        /*  4  uc_stack.ss_size           */
        /*
         * uc_mcontext.gregs[0..18] occupies words 5..23.
         *
         * The first six used to be an anonymous uu1[6].  They are named now
         * because %esi carries the guest system call argument block: a
         * guest loads the address of its own argument struct into %esi and
         * executes "int $0x80", and since Linux clobbers only %eax across
         * a trap, %esi arrives intact in the signal frame.  See
         * include/asm-six/sixcall.h for the protocol and system_call() in
         * arch/six/kernel/irq.c for the consumer.
         */
        unsigned int gs;                /*  5      gregs[GS]               */
        unsigned int fs;                /*  6      gregs[FS]               */
        unsigned int es;                /*  7      gregs[ES]               */
        unsigned int ds;                /*  8      gregs[DS]               */
        unsigned int edi;               /*  9      gregs[EDI]              */
        unsigned int esi;               /* 10      gregs[ESI]  <-- args    */
        unsigned int ebp;               /* 11      gregs[EBP]              */
        unsigned int kesp;              /* 12      gregs[ESP]              */
        unsigned int uu2[6];            /* 13..18  gregs[EBX..ERR]         */
        unsigned int pc;                /* 19      gregs[EIP]             */
        unsigned int cs;                /* 20      gregs[CS]              */
        unsigned int psw;               /* 21      gregs[EFL]             */
        unsigned int esp;               /* 22      gregs[UESP]            */
        unsigned int ss;                /* 23      gregs[SS]              */
        /*
         * fpregs is a POINTER into __fpregs_mem below.  getcontext() sets
         * it to point at the containing ucontext's own embedded area, so
         * it becomes a self-pointer -- and therefore dangles the moment a
         * context is copied somewhere else.  Anything that memcpy()s or
         * otherwise relocates a pt_regs must repoint it; use
         * six_fix_context() in arch/six/kernel/process.c.
         */
        unsigned int fpregs;            /* 24      uc_mcontext.fpregs     */
        unsigned int oldmask;           /* 25      uc_mcontext.oldmask    */
        unsigned int cr2;               /* 26      uc_mcontext.cr2        */
        unsigned int uc_sigmask[32];    /* 27..58  128-byte sigset_t      */
        unsigned int fpregs_mem[32];    /* 59..90  embedded FPU state     */

        /* --- end of the host's ucontext_t ----------------------------- */

        /*
         * SIX's own syscall "registers".  Deliberately outside the host
         * context, so context switching cannot disturb them.  Named g2..g9
         * for historical reasons: on SPARC they really are %g2..%g7.
         */
        unsigned int g2;                /* syscall number / return value  */
        unsigned int g3;                /* arg 1                          */
        unsigned int g4;                /* arg 2                          */
        unsigned int g5;                /* arg 3                          */
        unsigned int g6;                /* kernel_thread request marker   */
        unsigned int g7;
        unsigned int g8;
        unsigned int g9;
#else
        unsigned int uu1[6];
        unsigned int uc_sp;
        unsigned int uc_sp_size;
        unsigned int uu2[2];
        unsigned int psw;
        unsigned int pc;
        unsigned int npc;
        unsigned int uu3[2];
        unsigned int g2;
        unsigned int g3;
        unsigned int g4;
        unsigned int g5;
        unsigned int g6;
        unsigned int g7;
        unsigned uu4[6];
        unsigned int esp;
        unsigned uu5[84];
#endif
};

#if (__i386__)
/*
 * Build-time check that the overlay still lines up with the host.
 *
 * SIX_HOST_UCONTEXT_SIZE is what /tmp/uclayout reported for this glibc.
 * If a toolchain upgrade changes ucontext_t, this fails to compile rather
 * than silently corrupting every context switch -- which is exactly the
 * class of bug that made the original port so hard to debug.
 */
#define SIX_HOST_UCONTEXT_SIZE  364
#define SIX_PT_REGS_EXTRA       (8 * 4)         /* g2..g9 */

typedef int six_ptrace_layout_assert[
        (sizeof(struct pt_regs) ==
         SIX_HOST_UCONTEXT_SIZE + SIX_PT_REGS_EXTRA) ? 1 : -1];
#endif


#define user_mode(regs) (current->user_mode)
#define instruction_pointer(regs) ((regs)->pc)

#define USER_MODE 	1
#define KERNEL_MODE	0

#define go_user_mode()	current->user_mode = USER_MODE
#define go_kernel_mode()  current->user_mode = KERNEL_MODE 

#endif
