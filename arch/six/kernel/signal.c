
/*
 *  linux/arch/i386/kernel/signal.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/config.h>

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <asm/sigcontext.h>
#include <asm/segment.h>

#define _S(nr) (1<<((nr)-1))

#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

asmlinkage int sys_waitpid(pid_t pid,unsigned long * stat_addr, int options);
asmlinkage int do_signal(unsigned long oldmask, struct pt_regs * regs);

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage int sys_sigsuspend(struct pt_regs *regs, unsigned long *set)    
{       
        unsigned long mask;
        
        mask = current->blocked;
        current->blocked = *set & _BLOCKABLE;
#if (!SIX)
        regs->eax = -EINTR;
#else
	regs->g2 = -EINTR;
#endif
        while (1) {
                current->state = TASK_INTERRUPTIBLE;
                schedule();
                if (do_signal(mask, regs))
                        return -EINTR;
        }
}

/*
 * Basically the old context needs to be restored. 
 */
asmlinkage void sys_sigreturn(unsigned long sc, struct pt_regs *regs)
{
	struct sigcontext *scp;
	
	scp = (struct sigcontext *)sc;

	memcpy(regs, &scp->oldcon, sizeof(struct pt_regs));

#if (__i386__)
	/*
	 * The context we have just copied back carries the fpregs pointer
	 * that was valid where the copy was *taken from* -- the guest's
	 * signal frame.  glibc's ucontext_t is self-referential, so it now
	 * dangles.  Repoint it at this pt_regs' own embedded FPU area
	 * before anybody hands it to setcontext().
	 */
	six_fix_context(regs);
#endif
}

/*
 * Setup the signal stack frame; this IS going to be messy.
 */
static void setup_frame(struct sigaction *sa, struct pt_regs *regs, int signr,
        unsigned long oldmask)
{
	struct sigcontext *scp, sc;
	unsigned long sp;

	/*
	 * Where is the guest's stack pointer?
	 *
	 * On x86 the answer is annoyingly non-obvious.  pt_regs is an
	 * overlay on the host's ucontext_t, which has *two* stack pointer
	 * slots: gregs[REG_ESP] (our "kesp") and gregs[REG_UESP] (our
	 * "esp").  The kernel fills both identically when it delivers a
	 * signal, but glibc's setcontext() -- which is how SIX returns to
	 * a guest, see RESTORE_USER_CONTEXT -- reloads %esp from
	 * gregs[REG_ESP] and ignores REG_UESP completely.  The 2005 code
	 * only ever touched "esp", so every frame it built was silently
	 * discarded.  Read and write both; believe kesp.
	 */
#if (__i386__)
	sp = regs->kesp;
#else
	sp = regs->esp;
#endif

	/*
	 * We are basically about to take the current context and
	 * store it on the stack.
	 */
	scp = (struct sigcontext *)sp - 1;
	scp = (struct sigcontext *) stack_align((unsigned long)scp);

	memcpy(&sc.oldcon, regs, sizeof(struct pt_regs));
	/*
	 * Backup of signal mask
	 */
	sc.mask = oldmask;
	
	/*
	 * There - we have done it.
	 *
	 * Note this writes straight into guest memory.  That is legitimate
	 * under SIX: guest virtual addresses are host virtual addresses,
	 * and set_proc_mappings() has every page of `current' mapped.
	 */
	memcpy(scp, &sc, sizeof(struct sigcontext));


	/*
	 * Point the stack pointer to after the point where we stored the
	 * sigcontext structure.
	 * Now the stack will look like :
	 *
	 *	|			|
	 *	|-----------------------| <- old sp
	 *	|			|
	 *	|      sigcontext	|
	 	|      structure	| <- sigcontext structure that contains the
	 *	|			|    old context, old signal mask etc.
	 *	|			|
	 *	|-----------------------| <- this address is stored in register G6.
	 *	|			|    And the libc sigreturn() later uses this
	 *	|    sparc frame	|    to find a copy of the old context, which
	 *	|    (96 bytes)		|    it passes to the sigcontext syscall.
	 *	|			|
	 *	|-----------------------| <- new sp
	 *	|			|
	 *
	 */

#if (__i386__)
	/*
	 * x86 has no global registers to smuggle things through, so the
	 * trampoline is entered as an ordinary cdecl function instead:
	 *
	 *	int sigreturn(struct sigcontext *sc, int (*handler)());
	 *
	 * We hand-build its call frame below the sigcontext.  There is no
	 * real return address -- sigreturn() never returns, it finishes
	 * with a sigreturn(2) trap that restores oldcon -- so slot 0 is a
	 * deliberate zero.  If a guest ever does return from it, it faults
	 * at address 0, which is at least a legible way to die.
	 *
	 *	|-----------------------|
	 *	|   handler  (arg 1)	|  new sp + 8
	 *	|   scp      (arg 0)	|  new sp + 4
	 *	|   0        (ret addr)	|  new sp      <- 16-byte aligned + 4
	 *	|-----------------------|
	 *
	 * The alignment is what the i386 ABI guarantees a function at its
	 * entry point: %esp+4 is a multiple of 16, I.e. the stack was
	 * 16-aligned immediately before the (notional) call pushed the
	 * return address.  Gcc is entitled to use movaps on locals.
	 */
	{
		unsigned long *fp;
		unsigned long  fsp;

		fsp  = (unsigned long)scp;
		fsp -= 32;		/* keep clear of the sigcontext */
		fsp &= ~15UL;		/* 16-align ...                 */
		fsp -= 4;		/* ... then push the ret slot   */

		fp = (unsigned long *)fsp;
		fp[0] = 0;
		fp[1] = (unsigned long)scp;
		fp[2] = (unsigned long)sa->sa_handler;

		regs->kesp = fsp;	/* the one setcontext() reads */
		regs->esp  = fsp;	/* keep the shadow consistent */
	}
#else
	regs->esp = (unsigned long)scp - 96;

	regs->g6 = scp;
	regs->g7 = sa->sa_handler;
#endif
	/*
	 * Point pc to libc's very own sigreturn function. So yes,
	 * now the pc points to an execution address lying in userland.
	 * The libc sigreturn() obtains the actual desired handlers's
	 * address from register G7, and calls it. Once that's done,
	 * it retrieves the sigcontext address from G7, and passes it on
	 * as argument to a sigreturn() system call - which accepts
	 * the sigcontext argument, and restores the context lying in
	 * the oldcon member of struct sigcontext. Go to sched.h for
	 * more details.  (On x86 both of those travel on the stack as
	 * ordinary arguments instead; see above.)
	 */
	regs->pc = current->_sigreturn;
#if (!__i386__)
	regs->npc = regs->pc + 4;
#endif
}

/*
 * OK, we're invoking a handler
 */
static void handle_signal(unsigned long signr, struct sigaction *sa,
        unsigned long oldmask, struct pt_regs * regs)
{
        /* set up the stack frame */
        setup_frame(sa, regs, signr, oldmask);

        if (sa->sa_flags & SA_ONESHOT)
                sa->sa_handler = NULL;
        if (!(sa->sa_flags & SA_NOMASK))
                current->blocked |= (sa->sa_mask | _S(signr)) & _BLOCKABLE;
}


/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 *
 * Note that we go through the signals twice: once to check the signals that
 * the kernel can handle, and then we build all the user-level signal handling
 * stack-frames in one go after that.
 */

asmlinkage int do_signal(unsigned long oldmask, struct pt_regs * regs)
{
        unsigned long mask = ~current->blocked;
        unsigned long signr;
        struct sigaction * sa;

        while ((signr = current->signal & mask)) {
                signr = ffz(~signr);
                clear_bit(signr, &current->signal);
                sa = current->sig->action + signr;
                signr++;
                if ((current->flags & PF_PTRACED) && signr != SIGKILL) {
                        current->exit_code = signr;
                        current->state = TASK_STOPPED;
                        notify_parent(current);
                        schedule();
                        if (!(signr = current->exit_code))
                                continue;
                        current->exit_code = 0;
                        if (signr == SIGSTOP)
                                continue;
                        if (_S(signr) & current->blocked) {
                                current->signal |= _S(signr);
                                continue;
                        }
                        sa = current->sig->action + signr - 1;
                }
                if (sa->sa_handler == SIG_IGN) {
                        if (signr != SIGCHLD)
                                continue;
                        /* check for SIGCHLD: it's special */
                        while (sys_waitpid(-1,NULL,WNOHANG) > 0)
                                /* nothing */;
                        continue;
                }
                if (sa->sa_handler == SIG_DFL) {
                        if (current->pid == 1)
                                continue;
                        switch (signr) {
                        case SIGCONT: case SIGCHLD: case SIGWINCH:
                                continue;

                        case SIGTSTP: case SIGTTIN: case SIGTTOU:
                                if (is_orphaned_pgrp(current->pgrp) &&
                                    !(current->p_pptr && current->p_pptr->pid > 1))
                                        continue;
                        case SIGSTOP:
                                if (current->flags & PF_PTRACED)
                                        continue;
                                current->state = TASK_STOPPED;
                                current->exit_code = signr;
                                if (!(current->p_pptr->sig->action[SIGCHLD-1].sa_flags &
                                                SA_NOCLDSTOP))
                                        notify_parent(current);
                                schedule();
                                continue;

                        case SIGQUIT: case SIGILL: case SIGTRAP:
                        case SIGABRT: case SIGFPE: case SIGSEGV:
                                if (current->binfmt && current->binfmt->core_dump) {
                                        if (current->binfmt->core_dump(signr, regs))
                                                signr |= 0x80;
                                }
                                /* fall through */
                        default:
                                current->signal |= _S(signr & 0x7f);
                                current->flags |= PF_SIGNALED;
                                do_exit(signr);
                        }
                }
		else if (current->_sigreturn <= 0) {
			/*
			 * Some screwup.
			 */
                         current->signal |= _S(signr & 0x7f);
                         current->flags |= PF_SIGNALED;
                         do_exit(signr);
		}
		/* else
		 * handle the damn thing!
		 */
                handle_signal(signr, sa, oldmask, regs);
                return 1;
        }
        return 0;
}

