
#ifndef _ASMi386_SIGNAL_H
#define _ASMi386_SIGNAL_H

#define _NSIG	37
#define NSIG            _NSIG

/*
 * there definitions are straight from /usr/include/sys/signal.h
 */
#define SIGHUP  1       /* hangup */
#define SIGINT  2       /* interrupt (rubout) */
#define SIGQUIT 3       /* quit (ASCII FS) */
#define SIGILL  4       /* illegal instruction (not reset when caught) */
#define SIGTRAP 5       /* trace trap (not reset when caught) */
#define SIGIOT  6       /* IOT instruction */
#define SIGABRT 6       /* used by abort, replace SIGIOT in the future */
#define SIGEMT  7       /* EMT instruction */
#define SIGFPE  8       /* floating point exception */
#define SIGKILL 9       /* kill (cannot be caught or ignored) */
#define SIGBUS  10      /* bus error */
#define SIGSEGV 11      /* segmentation violation */
#define SIGSYS  12      /* bad argument to system call */
#define SIGPIPE 13      /* write on a pipe with no one to read it */
#define SIGALRM 14      /* alarm clock */
#define SIGTERM 15      /* software termination signal from kill */
#define SIGUSR1 16      /* user defined signal 1 */
#define SIGUSR2 17      /* user defined signal 2 */
#define SIGCLD  18      /* child status change */
#define SIGCHLD 18      /* child status change alias (POSIX) */
#define SIGPWR  19      /* power-fail restart */
#define SIGWINCH 20     /* window size change */
#define SIGURG  21      /* urgent socket condition */
#define SIGPOLL 22      /* pollable event occured */
#define SIGIO   SIGPOLL /* socket I/O possible (SIGPOLL alias) */
#define SIGSTOP 23      /* stop (cannot be caught or ignored) */
#define SIGTSTP 24      /* user stop requested from tty */
#define SIGCONT 25      /* stopped process has been continued */
#define SIGTTIN 26      /* background tty read attempted */
#define SIGTTOU 27      /* background tty write attempted */
#define SIGVTALRM 28    /* virtual timer expired */
#define SIGPROF 29      /* profiling timer expired */
#define SIGXCPU 30      /* exceeded cpu limit */
#define SIGXFSZ 31      /* exceeded file size limit */
#define SIGWAITING 32   /* process's lwps are blocked */
#define SIGLWP  33      /* special signal used by thread library */
#define SIGFREEZE 34    /* special signal used by CPR */
#define SIGTHAW 35      /* special signal used by CPR */
#define SIGCANCEL 36    /* thread cancellation signal used by libthread */
#define SIGLOST 37      /* resource lost (eg, record-lock lost) */

/* insert new signals here, and move _SIGRTM* appropriately */
#define _SIGRTMIN 38    /* first (highest-priority) realtime signal */
#define _SIGRTMAX 45    /* last (lowest-priority) realtime signal */

#define SIGSYSCALL SIGUSR1

/*
 * SIX_TRAPSIG -- the signal a process raises on itself to trap into the
 * SIX kernel.
 *
 * ---------------------------------------------------------------------
 * The signal numbers in the rest of this file are SOLARIS's, because this
 * header is a verbatim copy of Solaris's <sys/signal.h>.  Several of them
 * mean something different on Linux:
 *
 *      name        Solaris   Linux      SIX uses it as
 *      SIGLWP         33      (none)    syscall trap
 *      SIGVTALRM      28        26      timer IRQ
 *      SIGWINCH       20        28      window resize
 *      SIGPOLL        22        (SIGIO 29)  keyboard input
 *
 * Note that the trap and the resize signals swap places: Solaris
 * SIGVTALRM is 28, which on Linux is SIGWINCH, so the timer tick would
 * have been delivered to the window-resize handler.  And NPTL reserves 32
 * and 33, so sigaction() flatly refuses to install a handler for SIGLWP.
 *
 * The reconciled Linux numbers live in arch/six/kernel/host.h, which is
 * the single source of truth; this macro just forwards to it so that
 * anything still saying SIX_TRAPSIG gets the right answer.  Do not add new
 * users -- call six_host_raise_trap() instead.
 * ---------------------------------------------------------------------
 */
#include "../../arch/six/kernel/host.h"

#define SIX_TRAPSIG SIX_HOST_TRAPSIG

/*
 * sa_flags values: SA_STACK is not currently supported, but will allow the
 * usage of signal stacks by using the (now obsolete) sa_restorer field in
 * the sigaction structure as a stack pointer. This is now possible due to
 * the changes in signal handling. LBT 010493.
 * SA_INTERRUPT is a no-op, but left due to historical reasons. Use the
 * SA_RESTART flag to get restarting signals (which were the default long ago)
 * SA_SHIRQ flag is for shared interrupt support on PCI and EISA.
 */
#define SA_SHIRQ	0x08000000
#define SA_INTERRUPT	0x01000000
#define SA_NOCLDSTOP	0x20000
#define SA_STACK	0x1
#define SA_NOMASK	0x02000000
#define SA_ONESHOT	0x04000000

/*
 * again from /usr/include/sys/signal.h
 */
#define SA_SIGINFO      0x00000008
#define SA_ONSTACK      0x00000001
#define SA_RESETHAND    0x00000002
#define SA_RESTART      0x00000004

#ifdef __KERNEL__
/*
 * These values of sa_flags are used only by the kernel as part of the
 * irq handling routines.
 *
 * SA_INTERRUPT is also used by the irq handling routines.
 */
#define SA_PROBE SA_ONESHOT
#define SA_SAMPLE_RANDOM SA_RESTART
#endif


#define SIG_BLOCK          1	/* for blocking signals */
#define SIG_UNBLOCK        2	/* for unblocking signals */
#define SIG_SETMASK        3	/* for setting the signal mask */

/* Type of a signal handler.  */
typedef void (*__sighandler_t)(int);

#define SIG_DFL	((__sighandler_t)0)	/* default signal handling */
#define SIG_IGN	((__sighandler_t)1)	/* ignore signal */
#define SIG_ERR	((__sighandler_t)-1)	/* error return from signal */

#if 0
typedef struct {
        unsigned long   __sigbits[4];
} sigset_t;
#else
typedef unsigned long sigset_t;
#endif


struct  sigaction  {
        int sa_flags;
        union {
                void (*_handler)();
                void (*_sigaction)(int, void  *, void *);
        }       _funcptr;
        sigset_t sa_mask;
        int sa_resv[2];
};

#define sa_handler      _funcptr._handler
#define sa_sigaction    _funcptr._sigaction

#define MASK_LEN 4

/*
 * this is specific to solaris.
 */
typedef struct {
    unsigned mask[MASK_LEN];
} so_sigset_t;

struct  solaris_sigaction  {
        int sa_flags;
        union {
                void (*_handler)();
                void (*_sigaction)(int, void  *, void *);
        }       _funcptr;
        so_sigset_t sa_mask;
        int sa_resv[2];
};

#define sigemptyset(set) (memset((set), 0, sizeof(sigset_t)))
#define sigfullset(set) (memset((set), 0xff, sizeof(sigset_t)))

#define sosigemptyset(set) (memset((set), 0, sizeof(so_sigset_t)))
#define sosigfullset(set) (memset((set), 0xff, sizeof(so_sigset_t)))

#define sigword(sig) ((((sig) - 1) / 32) % MASK_LEN)
#define sigbit(sig) (1 << (((sig) - 1) % 32))

#define sosigaddset(set, sig) ((set)->mask[sigword(sig)] |= sigbit(sig))
#define sosigdelset(set, sig) ((set)->mask[sigword(sig)] &= ~sigbit(sig))

#endif
