
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
 * atomically swap in the new signal mask, and wait for a signal.
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
 * basically the old context needs to be restored. 
 */
asmlinkage void sys_sigreturn(unsigned long sc, struct pt_regs *regs)
{
	struct sigcontext *scp;
	
	scp = (struct sigcontext *)sc;

	memcpy(regs, &scp->oldcon, sizeof(struct pt_regs));
}

/*
 * setup the signal stack frame; this IS going to be messsy.
 */
static void setup_frame(struct sigaction *sa, struct pt_regs *regs, int signr,
        unsigned long oldmask)
{
	struct sigcontext *scp, sc;

	/*
	 * we are basically abt to take the current context and
	 * store it on the stack.
	 */
	scp = (struct sigcontext *)regs->esp - 1;
	scp = (struct sigcontext *) stack_align((unsigned long)scp);

	memcpy(&sc.oldcon, regs, sizeof(struct pt_regs));
	/*
	 * backup of signal mask
	 */
	sc.mask = oldmask;
	
	/*
	 * there - we have done it.
	 */
	memcpy(scp, &sc, sizeof(struct sigcontext));


	/*
	 * point the stackpointer to after the point where we stored the
	 * sigcontext structure.
	 * now the stack will look like :
	 *
	 *	|			|
	 *	|-----------------------| <- old sp
	 *	|			|
	 *	|      sigcontext	|
	 	|      structure	| <- sigcontext struture that contains the
	 *	|			|    old context, old signal mask etc.
	 *	|			|
	 *	|-----------------------| <- this address is stored in register G6.
	 *	|			|    and the libc sigreturn() later uses this
	 *	|    sparc frame	|    to find a copy of the old context, which
	 *	|    (96 bytes)		|    it passes to the sigcontext syscall.
	 *	|			|
	 *	|-----------------------| <- new sp
	 *	|			|
	 *
	 */

	regs->esp = (unsigned long)scp - 96;

	regs->g6 = scp;
	regs->g7 = sa->sa_handler;
	/*
	 * point pc to libc's very own sigreturn function. so yes,
	 * now the pc points to an execution address lying in userland.
	 * the libc sigreturn() obtains the actual desired handlers's
	 * address from register G7, and calls it. once thats done,
	 * it retrieves the sigcontext address from G7, and passes it on
	 * as argument to a sigreturn() system call - which accepts
	 * the sigcontext argument, and restores the context lying in
	 * the oldcon member of struct sigcontext. go to sched.h for
	 * more details.
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
                                if (is_orphaned_pgrp(current->pgrp))
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
			 * some screwup.
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

