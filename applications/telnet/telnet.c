/*
 * Telnet - User Interface to the TELNET Protocol for SIX
 *
 * Implements RFC 854 Telnet client with option negotiation
 * (ECHO, SUPPRESS-GO-AHEAD, TERMINAL-TYPE, NAWS), raw terminal mode,
 * escape character handling ('^]'), and interactive command prompt.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <linux/termios.h>
#include <linux/time.h>
#include <linux/types.h>

extern int close(int fd);
extern int read(int fd, void *buf, size_t count);
extern int write(int fd, const void *buf, size_t count);
extern int tcgetattr(int fd, struct termios *termios_p);
extern int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);
extern int select(int n, fd_set *inp, fd_set *outp, fd_set *exp, struct timeval *tvp);
extern int ioctl(int fd, int request, ...);

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

enum telnet_state {
	TS_DATA,
	TS_IAC,
	TS_WILL,
	TS_WONT,
	TS_DO,
	TS_DONT,
	TS_SB,
	TS_SB_DATA,
	TS_SB_IAC,
	TS_CR
};

static struct termios orig_termios;
static int raw_mode_active = 0;
static int current_sock = -1;
static char connected_host[128] = "";
static int connected_port = 23;

static void enable_raw_mode(void)
{
	struct termios raw;
	if (tcgetattr(0, &orig_termios) == 0) {
		raw = orig_termios;
		raw.c_lflag &= ~(ECHO | ICANON | ISIG);
		raw.c_iflag &= ~(IXON | ICRNL);
		raw.c_cc[VMIN] = 1;
		raw.c_cc[VTIME] = 0;
		tcsetattr(0, TCSANOW, &raw);
		raw_mode_active = 1;
	}
}

static void disable_raw_mode(void)
{
	if (raw_mode_active) {
		tcsetattr(0, TCSANOW, &orig_termios);
		raw_mode_active = 0;
	}
}

static void get_window_size(int *rows, int *cols)
{
	struct winsize ws;
	if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_row && ws.ws_col) {
		*rows = ws.ws_row;
		*cols = ws.ws_col;
	} else {
		*rows = 24;
		*cols = 80;
	}
}

static unsigned char opt_buf[128];
static int opt_len = 0;

static void queue_opt(unsigned char cmd, unsigned char opt)
{
	if (opt_len + 3 <= sizeof(opt_buf)) {
		opt_buf[opt_len++] = IAC;
		opt_buf[opt_len++] = cmd;
		opt_buf[opt_len++] = opt;
	}
}

static void queue_naws(void)
{
	int rows, cols;
	get_window_size(&rows, &cols);
	if (opt_len + 9 <= sizeof(opt_buf)) {
		opt_buf[opt_len++] = IAC;
		opt_buf[opt_len++] = SB;
		opt_buf[opt_len++] = TELOPT_NAWS;
		opt_buf[opt_len++] = (cols >> 8) & 0xFF;
		opt_buf[opt_len++] = cols & 0xFF;
		opt_buf[opt_len++] = (rows >> 8) & 0xFF;
		opt_buf[opt_len++] = rows & 0xFF;
		opt_buf[opt_len++] = IAC;
		opt_buf[opt_len++] = SE;
	}
}

static void queue_ttype(void)
{
	if (opt_len + 12 <= sizeof(opt_buf)) {
		opt_buf[opt_len++] = IAC;
		opt_buf[opt_len++] = SB;
		opt_buf[opt_len++] = TELOPT_TTYPE;
		opt_buf[opt_len++] = 0; /* IS */
		strcpy((char *)&opt_buf[opt_len], "vt100");
		opt_len += 5;
		opt_buf[opt_len++] = IAC;
		opt_buf[opt_len++] = SE;
	}
}

static int telnet_read_line(char *buf, int max)
{
	int pos = 0;
	char ch;
	while (pos < max - 1) {
		if (read(0, &ch, 1) <= 0) break;
		if (ch == '\r' || ch == '\n') {
			putchar('\n');
			fflush(stdout);
			break;
		} else if (ch == '\b' || ch == 127) {
			if (pos > 0) {
				pos--;
				printf("\b \b");
				fflush(stdout);
			}
		} else if (ch >= 32 && ch <= 126) {
			buf[pos++] = ch;
			putchar(ch);
			fflush(stdout);
		}
	}
	buf[pos] = '\0';
	return pos;
}

static int connect_to_host(const char *host, int port)
{
	struct sockaddr_in saddr;
	struct hostent *he;
	char ip_buf[32];
	int sfd;

	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons((unsigned short)port);

	/* Check if host is numeric IP */
	if (inet_aton(host, &saddr.sin_addr)) {
		strcpy(ip_buf, host);
	} else {
		he = gethostbyname(host);
		if (!he) {
			printf("telnet: could not resolve %s\n", host);
			return -1;
		}
		memcpy(&saddr.sin_addr, he->h_addr_list[0], he->h_length);
		strcpy(ip_buf, inet_ntoa(saddr.sin_addr));
	}

	printf("Trying %s...\n", ip_buf);
	fflush(stdout);

	sfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sfd < 0) {
		printf("telnet: socket creation failed\n");
		return -1;
	}

	if (connect(sfd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
		printf("telnet: Unable to connect to remote host: Connection refused\n");
		close(sfd);
		return -1;
	}

	printf("Connected to %s.\n", host);
	printf("Escape character is '^]'.\n");
	fflush(stdout);

	strncpy(connected_host, host, sizeof(connected_host) - 1);
	connected_port = port;
	current_sock = sfd;
	return sfd;
}

static void telnet_session(int sfd)
{
	enum telnet_state state = TS_DATA;
	unsigned char sb_opt = 0;
	unsigned char sock_buf[1024];
	unsigned char user_buf[256];
	int running = 1;
	enable_raw_mode();

	/* Announce initial client capabilities to remote telnet server (only on port 23) */
	if (connected_port == 23) {
		queue_opt(DO, TELOPT_SGA);
		queue_opt(DO, TELOPT_ECHO);
		queue_opt(WILL, TELOPT_TTYPE);
		queue_opt(WILL, TELOPT_NAWS);
		queue_naws();
		if (opt_len > 0) {
			write(sfd, opt_buf, opt_len);
			opt_len = 0;
		}
	}

	while (running) {
		fd_set rfds;
		int maxfd;
		int r;

		FD_ZERO(&rfds);
		FD_SET(0, &rfds);
		FD_SET(sfd, &rfds);
		maxfd = (sfd > 0) ? sfd : 0;

		r = select(maxfd + 1, &rfds, NULL, NULL, NULL);
		if (r < 0) break;

		/* 1. Inbound data from remote host */
		if (FD_ISSET(sfd, &rfds)) {
			int n = read(sfd, sock_buf, sizeof(sock_buf));
			int i;
			if (n <= 0) {
				disable_raw_mode();
				printf("\r\nConnection closed by foreign host.\r\n");
				break;
			}

			for (i = 0; i < n; i++) {
				unsigned char b = sock_buf[i];

				switch (state) {
				case TS_DATA:
					if (b == IAC) {
						state = TS_IAC;
					} else if (b == '\r') {
						state = TS_CR;
						write(1, "\r", 1);
					} else {
						write(1, &b, 1);
					}
					break;

				case TS_CR:
					if (b == '\0') {
						/* Bare CR: ignore trailing NUL per RFC 854 */
						state = TS_DATA;
					} else if (b == '\n') {
						write(1, "\n", 1);
						state = TS_DATA;
					} else if (b == IAC) {
						state = TS_IAC;
					} else {
						write(1, &b, 1);
						state = TS_DATA;
					}
					break;

				case TS_IAC:
					switch (b) {
					case IAC:
						write(1, &b, 1);
						state = TS_DATA;
						break;
					case WILL: state = TS_WILL; break;
					case WONT: state = TS_WONT; break;
					case DO:   state = TS_DO; break;
					case DONT: state = TS_DONT; break;
					case SB:   state = TS_SB; break;
					case AYT:
						write(sfd, "\r\n[SIX-telnet]\r\n", 16);
						state = TS_DATA;
						break;
					default:
						state = TS_DATA;
						break;
					}
					break;

				case TS_WILL:
					if (b == TELOPT_ECHO || b == TELOPT_SGA) {
						queue_opt(DO, b);
					} else {
						queue_opt(DONT, b);
					}
					state = TS_DATA;
					break;

				case TS_WONT:
					queue_opt(DONT, b);
					state = TS_DATA;
					break;

				case TS_DO:
					if (b == TELOPT_TTYPE) {
						queue_opt(WILL, TELOPT_TTYPE);
					} else if (b == TELOPT_NAWS) {
						queue_opt(WILL, TELOPT_NAWS);
						queue_naws();
					} else if (b == TELOPT_SGA) {
						queue_opt(WILL, TELOPT_SGA);
					} else {
						queue_opt(WONT, b);
					}
					state = TS_DATA;
					break;

				case TS_DONT:
					queue_opt(WONT, b);
					state = TS_DATA;
					break;

				case TS_SB:
					sb_opt = b;
					state = TS_SB_DATA;
					break;

				case TS_SB_DATA:
					if (b == IAC) {
						state = TS_SB_IAC;
					} else if (sb_opt == TELOPT_TTYPE && b == 1) {
						queue_ttype();
					}
					break;

				case TS_SB_IAC:
					if (b == SE) {
						state = TS_DATA;
					} else {
						state = TS_SB_DATA;
					}
					break;
				}
			}
			if (opt_len > 0) {
				write(sfd, opt_buf, opt_len);
				opt_len = 0;
			}
		}

		/* 2. Outbound data from local keyboard */
		if (FD_ISSET(0, &rfds)) {
			int n = read(0, user_buf, sizeof(user_buf));
			int i;
			unsigned char out_buf[512];
			int out_len = 0;

			if (n <= 0) break;

			for (i = 0; i < n; i++) {
				unsigned char ch = user_buf[i];

				/* Check for escape character ^] (0x1D) */
				if (ch == 29) {
					if (out_len > 0) {
						write(sfd, out_buf, out_len);
						out_len = 0;
					}
					char cmd[64];
					int in_cmd = 1;
					disable_raw_mode();
					while (in_cmd) {
						printf("\r\ntelnet> ");
						fflush(stdout);
						int len = telnet_read_line(cmd, sizeof(cmd));
						if (len <= 0) {
							/* Empty line / Enter: resume session */
							in_cmd = 0;
							break;
						}
						if (strcmp(cmd, "close") == 0 || strcmp(cmd, "c") == 0) {
							running = 0;
							in_cmd = 0;
							break;
						} else if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "q") == 0) {
							close(sfd);
							current_sock = -1;
							exit(0);
						} else if (strcmp(cmd, "status") == 0) {
							printf("Connected to %s on port %d.\r\n", connected_host, connected_port);
							printf("Escape character is '^]'.\r\n");
						} else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
							printf("Commands:\r\n");
							printf("  close   close current connection\r\n");
							printf("  status  print status information\r\n");
							printf("  quit    exit telnet\r\n");
							printf("  <Enter> resume session\r\n");
						} else {
							printf("?Invalid command. Type 'help' for available commands.\r\n");
						}
					}
					if (!running) break;
					enable_raw_mode();
				} else if (ch == '\r') {
					if (i + 1 < n && user_buf[i + 1] == '\n') {
						i++; /* Consume paired \n */
					}
					if (out_len + 2 < sizeof(out_buf)) {
						out_buf[out_len++] = '\r';
						out_buf[out_len++] = '\n';
					}
				} else if (ch == '\n') {
					if (out_len + 2 < sizeof(out_buf)) {
						out_buf[out_len++] = '\r';
						out_buf[out_len++] = '\n';
					}
				} else if (ch == IAC) {
					if (out_len + 2 < sizeof(out_buf)) {
						out_buf[out_len++] = IAC;
						out_buf[out_len++] = IAC;
					}
				} else {
					if (out_len < sizeof(out_buf) - 1) {
						out_buf[out_len++] = ch;
					}
				}
			}
			if (out_len > 0) {
				write(sfd, out_buf, out_len);
			}
		}
	}

	disable_raw_mode();
	close(sfd);
	current_sock = -1;
}

static void command_mode(void)
{
	char line[128];

	for (;;) {
		char *cmd;
		char *arg1;
		char *arg2;
		int port = 23;

		printf("telnet> ");
		fflush(stdout);

		if (telnet_read_line(line, sizeof(line)) <= 0)
			continue;

		cmd = strtok(line, " \t");
		if (!cmd) continue;

		if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "q") == 0) {
			break;
		} else if (strcmp(cmd, "open") == 0 || strcmp(cmd, "o") == 0) {
			arg1 = strtok(NULL, " \t");
			if (!arg1) {
				printf("usage: open host [port]\n");
				continue;
			}
			arg2 = strtok(NULL, " \t");
			if (arg2) port = atoi(arg2);
			if (port <= 0) port = 23;

			int sfd = connect_to_host(arg1, port);
			if (sfd >= 0) {
				telnet_session(sfd);
			}
		} else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
			printf("Commands:\n");
			printf("  open <host> [port]   connect to remote host\n");
			printf("  close                close current connection\n");
			printf("  status               print status\n");
			printf("  quit                 exit telnet\n");
		} else {
			printf("?Invalid command. Type 'help' for available commands.\n");
		}
	}
}

int main(int argc, char **argv)
{
	const char *host = NULL;
	int port = 23;

	if (argc > 1) {
		if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
			printf("Usage: telnet [host [port]]\n");
			printf("Connect to remote TELNET host.\n");
			printf("Default port is 23.\n");
			return 0;
		}
		host = argv[1];
		if (argc > 2) {
			port = atoi(argv[2]);
			if (port <= 0) port = 23;
		}
	}

	if (host) {
		int sfd = connect_to_host(host, port);
		if (sfd < 0) return 1;
		telnet_session(sfd);
		return 0;
	}

	command_mode();
	return 0;
}
