/*
 *  arch/six/kernel/host.h
 *
 *  Interface to the host ABI shim (arch/six/kernel/host.c).
 *
 *  Safe to include from kernel code: it deliberately contains no host
 *  types, so it cannot drag glibc's signal definitions into files that
 *  already use the Solaris-shaped ones in include/asm-six/signal.h.
 */

#ifndef _SIX_HOST_H
#define _SIX_HOST_H

/*
 * Host signal numbers used by SIX.
 *
 * These are LINUX numbers and are intentionally kept apart from the
 * Solaris ones in include/asm-six/signal.h, which differ:
 *
 *      purpose        Solaris        Linux        note
 *      syscall trap   SIGLWP 33      38           NPTL reserves 32 and 33,
 *                                                 so sigaction() refuses
 *                                                 them; 38 is SIGRTMIN+4
 *                                                 and is free.
 *      timer tick     SIGVTALRM 28   SIGVTALRM 26 setitimer(ITIMER_VIRTUAL)
 *      window resize  SIGWINCH 20    SIGWINCH 28
 *      keyboard       SIGPOLL 22     SIGIO 29     STREAMS I_SETSIG on
 *                                                 Solaris; O_ASYNC +
 *                                                 F_SETOWN on Linux.
 *                                                 Linux 22 is SIGTTOU.
 *
 * Note the trap and the resize signals swap places relative to Solaris:
 * Solaris SIGVTALRM is 28, which on Linux is SIGWINCH.  Getting this wrong
 * means the timer tick is delivered to the window-resize handler.
 */
#define SIX_HOST_TRAPSIG   38          /* SIGRTMIN+4 on glibc/Linux */
#define SIX_HOST_TIMERSIG  26          /* Linux SIGVTALRM           */
#define SIX_HOST_WINCHSIG  28          /* Linux SIGWINCH            */
#define SIX_HOST_KBDSIG    29          /* Linux SIGIO               */

/* sun_handler()'s shape: a SA_SIGINFO handler. */
typedef void (*six_host_handler_t)(int, void *, void *);

void six_host_masks_init(void);
void six_host_cli(void);
void six_host_sti(void);
void six_host_block_signal(int signo);
void six_host_unblock_signal(int signo);
int  six_host_install_handler(int signo, six_host_handler_t fn);
void six_host_raise_trap(void);

/* Host terminal.  See the long comment in host.c. */
void six_host_get_winsize(int *rows, int *cols);
int  six_host_tty_open_raw(void);
void six_host_tty_restore(int fd);

#endif /* _SIX_HOST_H */
