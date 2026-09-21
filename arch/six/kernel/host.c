/*
 *  Arch/six/kernel/host.c
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
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <errno.h>

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

/* cli() -- block host signals, I.e. "disable interrupts". */
void six_host_cli(void)
{
	six_build_masks();
	sigprocmask(SIG_SETMASK, &six_kernel_mask, (sigset_t *)0);
}

/* sti() -- unblock everything, I.e. "enable interrupts". */
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
	struct sigaction ign, old_ttou, old_ttin;
	int fd;

	fd = open("/dev/tty", O_RDWR);
	if (fd < 0)
		return -1;

	/*
	 * Tcsetattr() on the controlling terminal from a *background*
	 * process group raises SIGTTOU at the caller, which by default
	 * stops it.  SIX gets run that way routinely -- under gdb, or from
	 * a shell that has put it in the background -- and the result was
	 * the kernel wedging inside kbd_init() with no clue as to why.
	 * Ignore job-control signals for the duration.
	 */
	memset(&ign, 0, sizeof ign);
	ign.sa_handler = SIG_IGN;
	sigaction(SIGTTOU, &ign, &old_ttou);
	sigaction(SIGTTIN, &ign, &old_ttin);

	if (tcgetattr(fd, &six_saved_termios) == 0)
		six_saved_termios_valid = 1;

	t = six_saved_termios;
	t.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	t.c_iflag &= ~(IXON | IXOFF | ICRNL | INLCR | ISTRIP | BRKINT);
	t.c_cc[VMIN]  = 1;
	t.c_cc[VTIME] = 0;
	tcsetattr(fd, TCSANOW, &t);

	sigaction(SIGTTOU, &old_ttou, (struct sigaction *)0);
	sigaction(SIGTTIN, &old_ttin, (struct sigaction *)0);

	/* Deliver SIGIO to us when the terminal becomes readable. */
	fcntl(fd, F_SETOWN, getpid());
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK | O_ASYNC);

	return fd;
}

/* Undo six_host_tty_open_raw().  Safe to call if it never succeeded. */
void six_host_tty_restore(int fd)
{
	struct sigaction ign, old_ttou;

	if (fd < 0 || !six_saved_termios_valid)
		return;

	memset(&ign, 0, sizeof ign);
	ign.sa_handler = SIG_IGN;
	sigaction(SIGTTOU, &ign, &old_ttou);

	tcsetattr(fd, TCSANOW, &six_saved_termios);

	sigaction(SIGTTOU, &old_ttou, (struct sigaction *)0);
}

static void six_usage(const char *prog, FILE *fp, int code)
{
	fprintf(fp,
		"Usage: %s [-w|--wait] [-s|--single] [-d|--disk <path>] [single]\n"
		"\n"
		"Options:\n"
		"  -w, --wait            Pause before boot and print host PID for gdb attach\n"
		"  -s, --single          Boot into built-in single-user shell (go>)\n"
		"  -d, --disk <path>     Root filesystem image (overrides $DISKFILE)\n"
		"  -H, --hostname <name> Guest hostname (overrides SIX_HOSTNAME and 'black')\n"
		"  -h, --help            Show this help message and exit\n"
		"\n"
		"Environment:\n"
		"  SIX_HOSTNAME          Guest hostname (default: black)\n"
		"\n"
		"Run 'halt' in the guest for a clean shutdown, or press Ctrl+\\ to quit immediately.\n",
		prog ? prog : "./six");
	exit(code);
}

const char *six_host_hostname = "black";

void six_host_parse_args(int argc, char *argv[],
			 int *single_out, int *wait_out,
			 const char **disk_out)
{
	static const struct option long_opts[] = {
		{ "wait",     no_argument,       0, 'w' },
		{ "single",   no_argument,       0, 's' },
		{ "disk",     required_argument, 0, 'd' },
		{ "hostname", required_argument, 0, 'H' },
		{ "help",     no_argument,       0, 'h' },
		{ 0,          0,                 0,  0  }
	};
	int c;

	*single_out = 0;
	*wait_out   = 0;
	*disk_out   = 0;

	/* Environment variable fallback */
	const char *env_host = getenv("SIX_HOSTNAME");
	if (env_host && env_host[0])
		six_host_hostname = env_host;

	while ((c = getopt_long(argc, argv, "wsd:H:h", long_opts, 0)) != -1) {
		switch (c) {
		case 'w':
			*wait_out = 1;
			break;
		case 's':
			*single_out = 1;
			break;
		case 'd':
			*disk_out = optarg;
			break;
		case 'H':
			six_host_hostname = optarg;
			break;
		case 'h':
			six_usage(argv[0], stdout, 0);
			break;
		default:
			six_usage(argv[0], stderr, 2);
			break;
		}
	}

	for (; optind < argc; optind++) {
		if (strcmp(argv[optind], "single") == 0)
			*single_out = 1;
		else {
			fprintf(stderr, "%s: unexpected argument '%s'\n",
				argv[0], argv[optind]);
			six_usage(argv[0], stderr, 2);
		}
	}
}

/* Host network bridge operations (unprivileged user-mode sockets) */

int six_host_net_socket(int type)
{
	int fd = socket(AF_INET, type, 0);
	if (fd >= 0) {
		int flags = fcntl(fd, F_GETFL, 0);
		fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	}
	return fd;
}

int six_host_net_connect(int fd, unsigned int ip, unsigned short port)
{
	struct sockaddr_in sin;
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = ip;
	sin.sin_port = htons(port);
	int ret = connect(fd, (struct sockaddr *)&sin, sizeof(sin));
	if (ret < 0 && (errno == EINPROGRESS || errno == EALREADY))
		return 0; /* In progress */
	return ret;
}

int six_host_net_poll_connected(int fd)
{
	struct pollfd pfd;
	pfd.fd = fd;
	pfd.events = POLLOUT;
	pfd.revents = 0;
	int ret = poll(&pfd, 1, 0);
	if (ret > 0) {
		if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
			return -1;
		if (pfd.revents & POLLOUT) {
			int err = 0;
			socklen_t len = sizeof(err);
			getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
			return (err == 0) ? 1 : -1;
		}
	}
	return 0;
}

int six_host_net_poll_readable(int fd)
{
	struct pollfd pfd;
	pfd.fd = fd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	int ret = poll(&pfd, 1, 0);
	if (ret > 0) {
		if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
			return -1;
		if (pfd.revents & POLLIN)
			return 1;
	}
	return 0;
}

int six_host_net_send(int fd, const void *buf, int len)
{
	int ret = send(fd, buf, len, MSG_DONTWAIT);
	if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
		return 0;
	return ret;
}

int six_host_net_recv(int fd, void *buf, int len)
{
	int ret = recv(fd, buf, len, MSG_DONTWAIT);
	if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
		return 0;
	return ret;
}

int six_host_net_dns_query(const void *req, int req_len, void *resp, int max_resp_len)
{
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0) return -1;

	struct sockaddr_in dns_addr;
	memset(&dns_addr, 0, sizeof(dns_addr));
	dns_addr.sin_family = AF_INET;
	dns_addr.sin_port = htons(53);
	dns_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

	if (sendto(s, req, req_len, 0, (struct sockaddr *)&dns_addr, sizeof(dns_addr)) < 0) {
		dns_addr.sin_addr.s_addr = inet_addr("8.8.8.8");
		if (sendto(s, req, req_len, 0, (struct sockaddr *)&dns_addr, sizeof(dns_addr)) < 0) {
			close(s);
			return -1;
		}
	}

	struct pollfd pfd;
	pfd.fd = s;
	pfd.events = POLLIN;
	pfd.revents = 0;
	int r = -1;
	if (poll(&pfd, 1, 2000) > 0 && (pfd.revents & POLLIN)) {
		r = recv(s, resp, max_resp_len, 0);
	}
	close(s);
	return r;
}

void six_host_net_close(int fd)
{
	if (fd >= 0)
		close(fd);
}

void six_host_idle_sleep(void)
{
	usleep(2000);
}

static pid_t tls_bridge_pid = -1;

void six_host_tls_bridge_cleanup(void)
{
	if (tls_bridge_pid > 0) {
		kill(tls_bridge_pid, SIGKILL);
		tls_bridge_pid = -1;
	}
}

void six_host_tls_bridge_init(void)
{
	/* Check if already listening on 127.0.0.1:18443 */
	int test_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (test_fd >= 0) {
		struct sockaddr_in sin;
		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = inet_addr("127.0.0.1");
		sin.sin_port = htons(18443);
		if (connect(test_fd, (struct sockaddr *)&sin, sizeof(sin)) == 0) {
			/* Already running */
			close(test_fd);
			return;
		}
		close(test_fd);
	}

	/* Find tls_bridge.py */
	static char script_path[1024];
	script_path[0] = '\0';

	if (access("port/tools/tls_bridge.py", R_OK) == 0) {
		strcpy(script_path, "port/tools/tls_bridge.py");
	} else {
		static char exe[1024];
		ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
		if (len > 0) {
			exe[len] = '\0';
			char *slash = strrchr(exe, '/');
			if (slash) {
				*slash = '\0';
				snprintf(script_path, sizeof(script_path), "%s/port/tools/tls_bridge.py", exe);
				if (access(script_path, R_OK) != 0) {
					script_path[0] = '\0';
				}
			}
		}
	}

	if (script_path[0] == '\0') {
		fprintf(stderr, "six: tls_bridge.py not found, HTTPS forwarding disabled\n");
		return;
	}

	pid_t pid = fork();
	if (pid == 0) {
		/* Child: redirect stdin, stdout, stderr to /dev/null */
		int devnull = open("/dev/null", O_RDWR);
		if (devnull >= 0) {
			dup2(devnull, 0);
			dup2(devnull, 1);
			dup2(devnull, 2);
			if (devnull > 2)
				close(devnull);
		}
		execlp("python3", "python3", script_path, (char *)NULL);
		_exit(1);
	} else if (pid > 0) {
		tls_bridge_pid = pid;
		atexit(six_host_tls_bridge_cleanup);
		/* Wait briefly for port to bind (up to 200ms) */
		int i;
		for (i = 0; i < 20; i++) {
			usleep(10000);
			int s = socket(AF_INET, SOCK_STREAM, 0);
			if (s >= 0) {
				struct sockaddr_in sin;
				memset(&sin, 0, sizeof(sin));
				sin.sin_family = AF_INET;
				sin.sin_addr.s_addr = inet_addr("127.0.0.1");
				sin.sin_port = htons(18443);
				if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) == 0) {
					close(s);
					break;
				}
				close(s);
			}
		}
	}
}

typedef struct {
	const char *dli_fname;
	void       *dli_fbase;
	const char *dli_sname;
	void       *dli_saddr;
} six_dl_info_t;

extern int dladdr(const void *addr, six_dl_info_t *info);

struct six_sym_entry {
	unsigned long addr;
	char name[56];
};

static struct six_sym_entry *six_symtab = NULL;
static int six_symtab_count = 0;
static int six_symtab_loaded = 0;

static void six_load_system_map(void)
{
	FILE *fp;
	char line[256];
	int cap = 4096;

	if (six_symtab_loaded)
		return;
	six_symtab_loaded = 1;

	fp = fopen("System.map", "r");
	if (!fp)
		return;
	six_symtab = (struct six_sym_entry *)malloc(cap * sizeof(struct six_sym_entry));
	if (!six_symtab) {
		fclose(fp);
		return;
	}
	while (fgets(line, sizeof(line), fp)) {
		unsigned long a;
		char type;
		char sname[128];
		if (sscanf(line, "%lx %c %127s", &a, &type, sname) == 3) {
			if (type == 'T' || type == 't' || type == 'W' || type == 'w') {
				if (six_symtab_count >= cap) {
					cap *= 2;
					six_symtab = (struct six_sym_entry *)realloc(
						six_symtab, cap * sizeof(struct six_sym_entry));
					if (!six_symtab) {
						six_symtab_count = 0;
						break;
					}
				}
				six_symtab[six_symtab_count].addr = a;
				strncpy(six_symtab[six_symtab_count].name, sname, 55);
				six_symtab[six_symtab_count].name[55] = '\0';
				six_symtab_count++;
			}
		}
	}
	fclose(fp);
}

int six_host_sprint_symbol(unsigned long addr, char *buf, int buflen)
{
	six_dl_info_t info;
	int i, best = -1;

	if (!buf || buflen <= 0)
		return 0;

	six_load_system_map();
	if (six_symtab && six_symtab_count > 0) {
		for (i = 0; i < six_symtab_count; i++) {
			if (six_symtab[i].addr <= addr)
				best = i;
			else
				break;
		}
		if (best >= 0 && (addr - six_symtab[best].addr) < 0x10000UL) {
			snprintf(buf, buflen, "%s+0x%lx",
				 six_symtab[best].name, addr - six_symtab[best].addr);
			return 1;
		}
	}

	if (dladdr((const void *)addr, &info) && info.dli_sname) {
		unsigned long off = addr - (unsigned long)info.dli_saddr;
		snprintf(buf, buflen, "%s+0x%lx", info.dli_sname, off);
		return 1;
	}
	buf[0] = '\0';
	return 0;
}

