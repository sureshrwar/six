/*
 * telnetd.c - Telnet Daemon for SIX (/bin/telnetd)
 *
 * Implements RFC 854 Telnet server with option negotiation,
 * PTY allocation (/dev/ptypX -> /dev/ttypX), and login/shell session management.
 *
 * Usage:
 *   telnetd [-p port] [-s] [-d]
 *
 * Options:
 *   -p port   Listen on port (default 23)
 *   -s        Spawn /bin/sh directly without login
 *   -d        Debug / foreground mode
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/socket.h>
#include <linux/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <linux/termios.h>

#define DEFAULT_PORT 23
#define MAX_PTYS 8
#define BUF_SIZE 4096

/* Telnet Command Codes (RFC 854) */
#define IAC   255
#define DONT  254
#define DO    253
#define WONT  252
#define WILL  251
#define SB    250
#define GA    249
#define EL    248
#define EC    247
#define AYT   246
#define AO    245
#define IP    244
#define BRK   243
#define DM    242
#define NOP   241
#define SE    240

/* Telnet Option Codes */
#define TELOPT_BINARY  0
#define TELOPT_ECHO    1
#define TELOPT_SGA     3
#define TELOPT_TTYPE  24
#define TELOPT_NAWS   31

static int opt_port = DEFAULT_PORT;
static int opt_direct_shell = 0;
static int opt_debug = 0;

static void sigchld_handler(int sig)
{
	while (waitpid(-1, NULL, WNOHANG) > 0)
		;
}

static int open_pty_pair(int *master_fd, int *slave_fd)
{
	int i;
	char ptym_path[32];
	char ptys_path[32];

	for (i = 0; i < MAX_PTYS; i++) {
		snprintf(ptym_path, sizeof(ptym_path), "/dev/ptyp%d", i);
		int mfd = open(ptym_path, O_RDWR);
		if (mfd < 0)
			continue;

		snprintf(ptys_path, sizeof(ptys_path), "/dev/ttyp%d", i);
		int sfd = open(ptys_path, O_RDWR);
		if (sfd < 0) {
			close(mfd);
			continue;
		}

		*master_fd = mfd;
		*slave_fd = sfd;
		return 0;
	}
	return -1;
}

static void send_telnet_opt(int fd, unsigned char cmd, unsigned char opt)
{
	unsigned char buf[3];
	buf[0] = IAC;
	buf[1] = cmd;
	buf[2] = opt;
	write(fd, buf, 3);
}

static int filter_telnet_input(int client_fd, unsigned char *in, int in_len, unsigned char *out, int max_out)
{
	int i = 0;
	int out_len = 0;

	while (i < in_len && out_len < max_out) {
		if (in[i] == IAC) {
			if (i + 1 >= in_len)
				break;
			unsigned char cmd = in[i + 1];

			if (cmd == IAC) {
				out[out_len++] = IAC;
				i += 2;
			} else if (cmd == DO || cmd == DONT || cmd == WILL || cmd == WONT) {
				if (i + 2 >= in_len)
					break;
				unsigned char opt = in[i + 2];
				if (cmd == DO) {
					/* Refuse unsupported options */
					if (opt != TELOPT_ECHO && opt != TELOPT_SGA)
						send_telnet_opt(client_fd, WONT, opt);
				} else if (cmd == WILL) {
					if (opt != TELOPT_SGA)
						send_telnet_opt(client_fd, DONT, opt);
				}
				i += 3;
			} else if (cmd == SB) {
				/* Subnegotiation: skip until SE */
				i += 2;
				while (i < in_len) {
					if (in[i] == IAC && i + 1 < in_len && in[i + 1] == SE) {
						i += 2;
						break;
					}
					i++;
				}
			} else {
				i += 2;
			}
		} else if (in[i] == '\r') {
			/* CR handling: map \r\0 or \r\n to \r */
			out[out_len++] = '\r';
			i++;
			if (i < in_len && (in[i] == '\0' || in[i] == '\n'))
				i++;
		} else {
			out[out_len++] = in[i++];
		}
	}
	return out_len;
}

static void handle_session(int client_fd)
{
	int master_fd = -1;
	int slave_fd = -1;
	int has_pty = (open_pty_pair(&master_fd, &slave_fd) == 0);

	/* Initial Telnet handshake */
	send_telnet_opt(client_fd, WILL, TELOPT_ECHO);
	send_telnet_opt(client_fd, WILL, TELOPT_SGA);
	send_telnet_opt(client_fd, DO, TELOPT_SGA);

	pid_t pid = fork();
	if (pid < 0) {
		if (has_pty) { close(master_fd); close(slave_fd); }
		close(client_fd);
		exit(1);
	}

	if (pid == 0) {
		/* Child: interactive shell or login */
		if (has_pty) {
			close(master_fd);
			close(client_fd);

			setsid();
#ifdef TIOCSCTTY
			ioctl(slave_fd, TIOCSCTTY, 1);
#endif
			dup2(slave_fd, 0);
			dup2(slave_fd, 1);
			dup2(slave_fd, 2);
			close(slave_fd);
		} else {
			/* Socket fallback */
			dup2(client_fd, 0);
			dup2(client_fd, 1);
			dup2(client_fd, 2);
			close(client_fd);
		}

		if (!opt_direct_shell && access("/bin/login", X_OK) == 0) {
			execl("/bin/login", "login", NULL);
		}
		execl("/bin/sh", "sh", "-i", NULL);
		exit(1);
	}

	/* Parent relay */
	if (has_pty) {
		close(slave_fd);

		unsigned char net_in[BUF_SIZE];
		unsigned char clean_buf[BUF_SIZE];
		unsigned char pty_buf[BUF_SIZE];

		int max_fd = (client_fd > master_fd ? client_fd : master_fd) + 1;

		while (1) {
			fd_set fds;
			FD_ZERO(&fds);
			FD_SET(client_fd, &fds);
			FD_SET(master_fd, &fds);

			int ret = select(max_fd, &fds, NULL, NULL, NULL);
			if (ret < 0) {
				if (errno == EINTR) continue;
				break;
			}

			if (FD_ISSET(client_fd, &fds)) {
				ssize_t n = read(client_fd, net_in, sizeof(net_in));
				if (n <= 0) break;
				int clean_len = filter_telnet_input(client_fd, net_in, n, clean_buf, sizeof(clean_buf));
				if (clean_len > 0) {
					write(master_fd, clean_buf, clean_len);
				}
			}

			if (FD_ISSET(master_fd, &fds)) {
				ssize_t n = read(master_fd, pty_buf, sizeof(pty_buf));
				if (n <= 0) break;
				write(client_fd, pty_buf, n);
			}
		}

		kill(pid, SIGHUP);
		close(master_fd);
		close(client_fd);
		waitpid(pid, NULL, 0);
	} else {
		/* In fallback mode, child runs directly on client_fd */
		close(client_fd);
		waitpid(pid, NULL, 0);
	}

	exit(0);
}

int main(int argc, char **argv)
{
	int i;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
			opt_port = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-s") == 0) {
			opt_direct_shell = 1;
		} else if (strcmp(argv[i], "-d") == 0) {
			opt_debug = 1;
		} else {
			fprintf(stderr, "usage: telnetd [-p port] [-s] [-d]\n");
			return 1;
		}
	}

	int sfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sfd < 0) {
		perror("telnetd: socket");
		return 1;
	}

	int opt = 1;
	setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	struct sockaddr_in saddr;
	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons((unsigned short)opt_port);
	saddr.sin_addr.s_addr = INADDR_ANY;

	if (bind(sfd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
		perror("telnetd: bind");
		close(sfd);
		return 1;
	}

	if (listen(sfd, 5) < 0) {
		perror("telnetd: listen");
		close(sfd);
		return 1;
	}

	printf("SIX-telnetd 1.0 listening on port %d\n", opt_port);
	fflush(stdout);

	if (!opt_debug) {
		pid_t bg = fork();
		if (bg < 0) {
			perror("telnetd: fork");
			return 1;
		}
		if (bg > 0) {
			/* Parent exits so daemon runs in background */
			return 0;
		}
		setsid();
	}

	signal(SIGPIPE, SIG_IGN);
	signal(SIGCHLD, sigchld_handler);

	for (;;) {
		struct sockaddr_in caddr;
		int clen = sizeof(caddr);
		int cfd = accept(sfd, (struct sockaddr *)&caddr, &clen);
		if (cfd < 0) {
			if (errno == EINTR) continue;
			break;
		}

		pid_t cpid = fork();
		if (cpid == 0) {
			close(sfd);
			handle_session(cfd);
		} else {
			close(cfd);
		}
	}

	close(sfd);
	return 0;
}
