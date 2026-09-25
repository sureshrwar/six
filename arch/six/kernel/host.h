/*
 *  Arch/six/kernel/host.h
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
void six_host_idle_sleep(void);

/* Command-line option parsing via host getopt_long(). */
extern const char *six_host_hostname;
void six_host_parse_args(int argc, char *argv[],
			 int *single_out, int *wait_out,
			 const char **disk_out);

/* Host unprivileged socket bridge operations */
int  six_host_net_socket(int type);
int  six_host_net_connect(int fd, unsigned int ip, unsigned short port);
int  six_host_net_poll_connected(int fd);
int  six_host_net_poll_readable(int fd);
int  six_host_net_send(int fd, const void *buf, int len);
int  six_host_net_recv(int fd, void *buf, int len);
int  six_host_net_dns_query(const void *req, int req_len, void *resp, int max_resp_len);
void six_host_net_close(int fd);
void six_host_tls_bridge_init(void);
void six_host_tls_bridge_cleanup(void);

/* Host sadb (SIX Android Debug Bridge) transport operations */
int  six_host_sadb_init(int *port_out, char *serial_out, int serial_len);
int  six_host_sadb_poll_accept(void);
int  six_host_sadb_accept(void);
int  six_host_sadb_recv(int fd, void *buf, int len);
int  six_host_sadb_send_all(int fd, const void *buf, int len);

/* Kernel symbol resolution for stack traces via dladdr(). */
int  six_host_sprint_symbol(unsigned long addr, char *buf, int buflen);

/* Host power management suspend entry (stops ITIMER_REAL and sleeps on wakeup IRQs) */
int  six_host_pm_suspend_enter(int wakealarm_ms, const int *sadb_fds, int num_sadb_fds,
			       char *wake_reason, int reason_len, unsigned long *slept_ms_out);

#endif /* _SIX_HOST_H */
