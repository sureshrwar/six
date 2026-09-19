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
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

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

/*
 * ---------------------------------------------------------------------
 * Host terminal
 *
 * The 2005 code spoke to the terminal with Solaris ioctl numbers built
 * inline, e.g. (( 'T' << 8 ) | 104 ) for TIOCGWINSZ and (('S'<<8)|011)
 * for the STREAMS I_SETSIG.  None of those numbers mean the same thing
 * on Linux, and they all quietly returned ENOTTY:
 *
 *      what SIX wanted   Solaris number   what Linux calls that number
 *      TIOCGWINSZ        0x5468           unassigned  -> ENOTTY
 *      TCGETA            0x540d           TIOCNXCL
 *      TCSETA            0x540e           TIOCSCTTY
 *      I_SETSIG          0x5309           unassigned  -> ENOTTY
 *
 * The TIOCGWINSZ failure was not cosmetic: con_setsize() left `winsize`
 * uninitialised, so video_screen_size came out 0, so con_type_init() set
 * video_mem_term == video_mem_base, so scr_writew()'s "is this address on
 * the screen?" test was false for every address and tga_blitc() -- the
 * function that actually write(2)s the character -- was never called.
 * The kernel booted perfectly and printed nothing but newlines.
 * ---------------------------------------------------------------------
 */

/* Saved terminal state, so we can put the user's shell back as we found it. */
static struct termios six_saved_termios;
static int            six_saved_termios_valid = 0;

/*
 * Ask the host how big the terminal is.
 *
 * Falls back to 25x80 when there is no tty (output redirected to a file or
 * a pipe, which is how this is usually run under strace/gdb).  Returning a
 * zero-sized console is never useful and is what broke the console before.
 */
void six_host_get_winsize(int *rows, int *cols)
{
	struct winsize win;

	if (ioctl(1, TIOCGWINSZ, &win) == 0 && win.ws_row && win.ws_col) {
		*rows = win.ws_row;
		*cols = win.ws_col;
		return;
	}

	*rows = 25;
	*cols = 80;
}

/*
 * Open the controlling terminal and put it in the mode SIX's emulated
 * keyboard controller expects: raw, non-blocking, and arranging for a
 * signal whenever input arrives.
 *
 * On Solaris the last part was the STREAMS I_SETSIG/S_RDNORM above, which
 * raises SIGPOLL (Solaris signal 22).  The Linux equivalent is O_ASYNC
 * with F_SETOWN, which raises SIGIO (29) -- see SIX_HOST_KBDSIG.  Note
 * that Linux signal 22 is SIGTTOU, so the old number had to change.
 *
 * Returns the fd, or -1.
 */
int six_host_tty_open_raw(void)
{
	struct termios t;
	int fd;

	fd = open("/dev/tty", O_RDWR);
	if (fd < 0)
		return -1;

	if (tcgetattr(fd, &six_saved_termios) == 0)
		six_saved_termios_valid = 1;

	t = six_saved_termios;
	t.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	t.c_iflag &= ~(IXON | IXOFF | ICRNL | INLCR | ISTRIP | BRKINT);
	t.c_cc[VMIN]  = 1;
	t.c_cc[VTIME] = 0;
	tcsetattr(fd, TCSANOW, &t);

	/* Deliver SIGIO to us when the terminal becomes readable. */
	fcntl(fd, F_SETOWN, getpid());
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK | O_ASYNC);

	return fd;
}

/* Undo six_host_tty_open_raw().  Safe to call if it never succeeded. */
void six_host_tty_restore(int fd)
{
	if (fd >= 0 && six_saved_termios_valid)
		tcsetattr(fd, TCSANOW, &six_saved_termios);
}
