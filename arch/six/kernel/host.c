/*
 *  arch/six/kernel/host.c
 *
 *  The host ABI shim.
 *
 *  SIX runs the kernel as an ordinary host process, so it has to talk to
 *  the host's signal machinery -- but include/asm-six/signal.h is a copy of
 *  *Solaris's* <sys/signal.h>, with Solaris's signal numbers, Solaris's
 *  `struct sigaction` field order and a 16-byte `so_sigset_t`.  Handing
 *  those structures to glibc does not work:
 *
 *    - Solaris orders sigaction as { sa_flags, sa_handler, sa_mask, ... }
 *      while glibc orders it { sa_handler, sa_mask, sa_flags, ... }.  The
 *      2005 code therefore passed SA_SIGINFO|SA_RESTART (== 12) where
 *      glibc reads sa_handler, and the first signal delivered jumped to
 *      address 0xc.
 *
 *    - glibc's sigset_t is 128 bytes; so_sigset_t is 16.  Passing the
 *      latter to sigprocmask() overruns the caller's stack.
 *
 *  Rather than rewrite asm-six/signal.h (which the whole kernel includes),
 *  this file is the single place that speaks the host's ABI.  It includes
 *  ONLY host headers -- never kernel ones -- and exports a small C
 *  interface that the kernel calls.  Keep it that way: if you need a
 *  kernel constant here, pass it in as a parameter.
 */

#include <signal.h>
#include <string.h>
#include <stddef.h>

#include "host.h"

/*
 * The "interrupts disabled" mask.
 *
 * Everything is blocked except the synchronous faults (which can only be
 * raised by the current instruction, so blocking them would just turn a
 * fault into an unrecoverable one), SIGINT (so the user can always kill a
 * wedged kernel), and the syscall trap itself -- SIX deliberately keeps
 * system calls working while "interrupts" are off.
 */
static sigset_t six_kernel_mask;
static sigset_t six_empty_mask;
static int      six_masks_ready;

static void six_build_masks(void)
{
	if (six_masks_ready)
		return;

	sigemptyset(&six_empty_mask);

	sigfillset(&six_kernel_mask);
	sigdelset(&six_kernel_mask, SIGILL);
	sigdelset(&six_kernel_mask, SIGTRAP);
	sigdelset(&six_kernel_mask, SIGFPE);
	sigdelset(&six_kernel_mask, SIGBUS);
	sigdelset(&six_kernel_mask, SIGSEGV);
	sigdelset(&six_kernel_mask, SIGSYS);
	sigdelset(&six_kernel_mask, SIGINT);
	sigdelset(&six_kernel_mask, SIX_HOST_TRAPSIG);

	six_masks_ready = 1;
}

void six_host_masks_init(void)
{
	six_build_masks();
}

/* cli() -- block host signals, i.e. "disable interrupts". */
void six_host_cli(void)
{
	six_build_masks();
	sigprocmask(SIG_SETMASK, &six_kernel_mask, (sigset_t *)0);
}

/* sti() -- unblock everything, i.e. "enable interrupts". */
void six_host_sti(void)
{
	six_build_masks();
	sigprocmask(SIG_SETMASK, &six_empty_mask, (sigset_t *)0);
}

void six_host_block_signal(int signo)
{
	sigset_t s;
	sigemptyset(&s);
	sigaddset(&s, signo);
	sigprocmask(SIG_BLOCK, &s, (sigset_t *)0);
}

void six_host_unblock_signal(int signo)
{
	sigset_t s;
	sigemptyset(&s);
	sigaddset(&s, signo);
	sigprocmask(SIG_UNBLOCK, &s, (sigset_t *)0);
}

/*
 * Install sun_handler() for one signal.
 *
 * Returns 0 on success, -1 if the host refused.  Refusal is expected and
 * harmless for SIGKILL/SIGSTOP and for the two signals glibc's threading
 * layer reserves (32 and 33) -- which is precisely why the syscall trap
 * can no longer be SIGLWP.
 */
int six_host_install_handler(int signo, six_host_handler_t fn)
{
	struct sigaction sa;

	six_build_masks();

	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = (void (*)(int, siginfo_t *, void *)) fn;
	sa.sa_flags     = SA_SIGINFO | SA_RESTART;
	sa.sa_mask      = six_kernel_mask;

	return sigaction(signo, &sa, (struct sigaction *)0);
}

/*
 * Raise the syscall trap on ourselves.  Equivalent to the old
 * getpid()+kill() dance in int0x80(), but expressed portably; the caller
 * is already inside the kernel, so there is no reason to bypass libc.
 */
void six_host_raise_trap(void)
{
	raise(SIX_HOST_TRAPSIG);
}
