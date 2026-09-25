/*
 * sadbd.c - SIX Android Debug Bridge Daemon (/bin/sadbd)
 *
 * In-guest daemon listening on /dev/sadb (major 61, minor 0) and serving
 * host `sadb` requests:
 *   - INFO / PING           (device discovery, state, properties)
 *   - EXEC <cmd>            (non-interactive shell command with exit code)
 *   - SHELL <r> <c> <term>  (interactive PTY shell with TIOCSWINSZ resize)
 *   - PUSH <path> <m> <sz>  (host -> guest file upload)
 *   - PULL <path>           (guest -> host file download)
 *   - STAT <path>           (file metadata query)
 *   - LOGCAT <mode>         (kernel dmesg + vold/sadbd system logs)
 *   - REBOOT <mode>         (guest sync & halt/reboot)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <linux/time.h>
#include <linux/termios.h>
#include <linux/sadb.h>

#define MAX_PTYS 8
#define BUF_SIZE 4096

static void log_msg(const char *fmt, const char *arg)
{
	int fd = open("/tmp/sadbd.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd >= 0) {
		char buf[512];
		int n = snprintf(buf, sizeof(buf), fmt, arg ? arg : "");
		if (n > 0)
			write(fd, buf, n);
		close(fd);
	}
}

static int write_all(int fd, const void *buf, int len)
{
	const unsigned char *p = (const unsigned char *)buf;
	int total = 0;
	while (total < len) {
		int w = write(fd, p + total, len - total);
		if (w <= 0) {
			if (w < 0 && errno == EINTR)
				continue;
			return total > 0 ? total : -1;
		}
		total += w;
	}
	return total;
}

static int read_line(int fd, char *buf, int maxlen)
{
	int i = 0;
	while (i < maxlen - 1) {
		char c;
		int r = read(fd, &c, 1);
		if (r <= 0) {
			if (r < 0 && errno == EINTR)
				continue;
			break;
		}
		if (c == '\r')
			continue;
		if (c == '\n')
			break;
		buf[i++] = c;
	}
	buf[i] = '\0';
	return i;
}

static int open_pty_pair(int *master_fd, int *slave_fd, char *tty_name, int tty_len)
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
		if (tty_name && tty_len > 0)
			strncpy(tty_name, ptys_path, tty_len - 1);
		return 0;
	}
	return -1;
}

static void handle_info(int cfd, const char *serial, int port)
{
	char hostname[64] = "six";
	char uptime_buf[64] = "0.00";
	char resp[1024];
	int fd, n;

	fd = open("/etc/hostname", O_RDONLY);
	if (fd >= 0) {
		n = read(fd, hostname, sizeof(hostname) - 1);
		if (n > 0) {
			hostname[n] = '\0';
			char *nl = strchr(hostname, '\n');
			if (nl) *nl = '\0';
		}
		close(fd);
	}

	fd = open("/proc/uptime", O_RDONLY);
	if (fd >= 0) {
		n = read(fd, uptime_buf, sizeof(uptime_buf) - 1);
		if (n > 0) {
			uptime_buf[n] = '\0';
			char *sp = strchr(uptime_buf, ' ');
			if (sp) *sp = '\0';
			char *nl = strchr(uptime_buf, '\n');
			if (nl) *nl = '\0';
		}
		close(fd);
	}

	n = snprintf(resp, sizeof(resp),
		     "OKAY\n"
		     "serial=%s\n"
		     "state=device\n"
		     "product=six_x86\n"
		     "model=SIX_Linux_2.0.11\n"
		     "device=%s\n"
		     "port=%d\n"
		     "uptime=%ss\n"
		     "features=shell_v2,exec,push,pull,logcat,stat,reboot\n",
		     serial, hostname, port, uptime_buf);
	write_all(cfd, resp, n);
}

static void handle_exec(int cfd, const char *cmd)
{
	int out_pipe[2];
	pid_t pid;
	int status = 0;
	int exit_code = 0;
	char buf[BUF_SIZE];
	char trailer[64];

	if (pipe(out_pipe) < 0) {
		const char *err = "FAIL pipe failed\n";
		write_all(cfd, err, strlen(err));
		return;
	}

	write_all(cfd, "OKAY\n", 5);

	pid = fork();
	if (pid < 0) {
		close(out_pipe[0]);
		close(out_pipe[1]);
		return;
	}

	if (pid == 0) {
		char *sh_argv[4];
		char *sh_envp[6];
		int null_fd;

		close(out_pipe[0]);
		close(cfd);

		null_fd = open("/dev/null", O_RDONLY);
		if (null_fd >= 0) {
			dup2(null_fd, 0);
			if (null_fd > 2)
				close(null_fd);
		}
		dup2(out_pipe[1], 1);
		dup2(out_pipe[1], 2);
		if (out_pipe[1] > 2)
			close(out_pipe[1]);

		sh_argv[0] = "sh";
		sh_argv[1] = "-c";
		sh_argv[2] = (char *)cmd;
		sh_argv[3] = NULL;

		sh_envp[0] = "PATH=/bin:/sbin:/usr/bin";
		sh_envp[1] = "HOME=/";
		sh_envp[2] = "TERM=vt100";
		sh_envp[3] = "USER=root";
		sh_envp[4] = NULL;

		execve("/bin/sh", sh_argv, sh_envp);
		_exit(127);
	}

	close(out_pipe[1]);
	for (;;) {
		int r = read(out_pipe[0], buf, sizeof(buf));
		if (r <= 0) {
			if (r < 0 && errno == EINTR)
				continue;
			break;
		}
		if (write_all(cfd, buf, r) < 0)
			break;
	}
	close(out_pipe[0]);

	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR)
			break;
	}
	if (WIFEXITED(status))
		exit_code = WEXITSTATUS(status);
	else if (WIFSIGNALED(status))
		exit_code = 128 + WTERMSIG(status);

	int tlen = snprintf(trailer, sizeof(trailer), "%c__SADB_EXIT__:%d\n", '\0', exit_code);
	write_all(cfd, trailer, tlen);
}

static void handle_shell(int cfd, const char *args)
{
	int rows = 24, cols = 80;
	char term[64] = "vt100";
	const char *cmd = NULL;
	int master_fd = -1, slave_fd = -1;
	char tty_name[32] = "/dev/ttyp0";
	int has_pty;
	pid_t pid;

	if (args && *args) {
		int consumed = 0;
		if (sscanf(args, "%d %d %63s%n", &rows, &cols, term, &consumed) >= 3 && consumed > 0) {
			const char *p = args + consumed;
			while (*p == ' ')
				p++;
			if (*p)
				cmd = p;
		}
	}

	has_pty = (open_pty_pair(&master_fd, &slave_fd, tty_name, sizeof(tty_name)) == 0);
	if (has_pty) {
		struct winsize ws;
		ws.ws_row = (rows > 0) ? rows : 24;
		ws.ws_col = (cols > 0) ? cols : 80;
		ws.ws_xpixel = 0;
		ws.ws_ypixel = 0;
		ioctl(slave_fd, TIOCSWINSZ, &ws);
	}

	write_all(cfd, "OKAY\n", 5);

	pid = fork();
	if (pid < 0) {
		if (has_pty) {
			close(master_fd);
			close(slave_fd);
		}
		return;
	}

	if (pid == 0) {
		char *sh_argv[4];
		char *sh_envp[7];
		char term_env[80];

		if (has_pty) {
			close(master_fd);
			close(cfd);
			setsid();
#ifdef TIOCSCTTY
			ioctl(slave_fd, TIOCSCTTY, 1);
#endif
			dup2(slave_fd, 0);
			dup2(slave_fd, 1);
			dup2(slave_fd, 2);
			if (slave_fd > 2)
				close(slave_fd);
		} else {
			setsid();
			dup2(cfd, 0);
			dup2(cfd, 1);
			dup2(cfd, 2);
			if (cfd > 2)
				close(cfd);
		}

		chdir("/");
		snprintf(term_env, sizeof(term_env), "TERM=%s", term);

		if (cmd && *cmd) {
			sh_argv[0] = "sh";
			sh_argv[1] = "-c";
			sh_argv[2] = (char *)cmd;
			sh_argv[3] = NULL;
		} else {
			sh_argv[0] = "-sh";
			sh_argv[1] = "-i";
			sh_argv[2] = NULL;
		}

		sh_envp[0] = "PATH=/bin:/sbin:/usr/bin";
		sh_envp[1] = "HOME=/";
		sh_envp[2] = term_env;
		sh_envp[3] = "USER=root";
		sh_envp[4] = "PS1=six:/# ";
		sh_envp[5] = NULL;

		execve("/bin/sh", sh_argv, sh_envp);
		_exit(1);
	}

	if (!has_pty) {
		waitpid(pid, NULL, 0);
		return;
	}

	close(slave_fd);
	{
		unsigned char in_buf[BUF_SIZE];
		unsigned char out_buf[BUF_SIZE];
		int max_fd = (cfd > master_fd ? cfd : master_fd) + 1;
		int child_exited = 0;

		for (;;) {
			fd_set rfds;
			struct timeval tv;
			int ret;

			if (!child_exited && waitpid(pid, NULL, WNOHANG) > 0)
				child_exited = 1;

			FD_ZERO(&rfds);
			if (!child_exited)
				FD_SET(cfd, &rfds);
			FD_SET(master_fd, &rfds);
			tv.tv_sec = 0;
			tv.tv_usec = child_exited ? 0 : 250000;

			ret = select(max_fd, &rfds, NULL, NULL, &tv);
			if (ret < 0) {
				if (errno == EINTR)
					continue;
				break;
			}
			if (child_exited && !FD_ISSET(master_fd, &rfds))
				break;

			if (!child_exited && FD_ISSET(cfd, &rfds)) {
				int n = read(cfd, in_buf, sizeof(in_buf));
				if (n <= 0)
					break;
				/* Check for in-band window resize frame: \x1dWINCH <rows> <cols>\n */
				if (n >= 10 && in_buf[0] == 0x1d && memcmp(in_buf + 1, "WINCH ", 6) == 0) {
					int nr = 0, nc = 0;
					if (sscanf((const char *)in_buf + 7, "%d %d", &nr, &nc) == 2 &&
					    nr > 0 && nc > 0) {
						struct winsize ws;
						ws.ws_row = nr;
						ws.ws_col = nc;
						ws.ws_xpixel = 0;
						ws.ws_ypixel = 0;
						ioctl(master_fd, TIOCSWINSZ, &ws);
						kill(-pid, SIGWINCH);
					}
					continue;
				}
				write_all(master_fd, in_buf, n);
			}

			if (FD_ISSET(master_fd, &rfds)) {
				int n = read(master_fd, out_buf, sizeof(out_buf));
				if (n <= 0)
					break;
				if (write_all(cfd, out_buf, n) < 0)
					break;
			}
		}

		if (!child_exited) {
			kill(-pid, SIGHUP);
			close(master_fd);
			waitpid(pid, NULL, 0);
		} else {
			close(master_fd);
		}
	}
}

static void handle_push(int cfd, const char *args)
{
	char path[512];
	unsigned int mode = 0644;
	unsigned long size = 0;
	int fd;
	unsigned long rem;
	char buf[BUF_SIZE];
	char done_msg[64];

	if (!args || sscanf(args, "%511s %o %lu", path, &mode, &size) != 3) {
		const char *err = "FAIL invalid PUSH arguments\n";
		write_all(cfd, err, strlen(err));
		return;
	}

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
	if (fd < 0) {
		char err[128];
		int n = snprintf(err, sizeof(err), "FAIL cannot open %s (errno=%d)\n", path, errno);
		write_all(cfd, err, n);
		return;
	}
	chmod(path, mode);

	write_all(cfd, "OKAY\n", 5);

	rem = size;
	while (rem > 0) {
		int to_read = rem < sizeof(buf) ? (int)rem : (int)sizeof(buf);
		int r = read(cfd, buf, to_read);
		if (r <= 0) {
			if (r < 0 && errno == EINTR)
				continue;
			break;
		}
		if (write_all(fd, buf, r) < 0)
			break;
		rem -= r;
	}
	close(fd);

	int n = snprintf(done_msg, sizeof(done_msg), "DONE %lu\n", size - rem);
	write_all(cfd, done_msg, n);
}

static void handle_pull(int cfd, const char *path)
{
	struct stat st;
	int fd;
	char hdr[128];
	char buf[BUF_SIZE];
	int n;

	if (!path || !*path || stat(path, &st) < 0) {
		n = snprintf(hdr, sizeof(hdr), "FAIL cannot stat %s (errno=%d)\n",
			     path ? path : "", errno);
		write_all(cfd, hdr, n);
		return;
	}
	if (S_ISDIR(st.st_mode)) {
		n = snprintf(hdr, sizeof(hdr), "FAIL %s is a directory\n", path);
		write_all(cfd, hdr, n);
		return;
	}

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		n = snprintf(hdr, sizeof(hdr), "FAIL cannot open %s (errno=%d)\n", path, errno);
		write_all(cfd, hdr, n);
		return;
	}

	n = snprintf(hdr, sizeof(hdr), "OKAY %04o %lu\n",
		     (unsigned int)(st.st_mode & 07777), (unsigned long)st.st_size);
	write_all(cfd, hdr, n);

	for (;;) {
		int r = read(fd, buf, sizeof(buf));
		if (r <= 0) {
			if (r < 0 && errno == EINTR)
				continue;
			break;
		}
		if (write_all(cfd, buf, r) < 0)
			break;
	}
	close(fd);
}

static void handle_stat(int cfd, const char *path)
{
	struct stat st;
	char resp[256];
	int n;

	if (!path || !*path || stat(path, &st) < 0) {
		n = snprintf(resp, sizeof(resp), "FAIL %d\n", errno);
		write_all(cfd, resp, n);
		return;
	}
	n = snprintf(resp, sizeof(resp), "OKAY mode=%04o size=%lu uid=%d gid=%d mtime=%lu\n",
		     (unsigned int)(st.st_mode & 077777),
		     (unsigned long)st.st_size,
		     (int)st.st_uid, (int)st.st_gid,
		     (unsigned long)st.st_mtime);
	write_all(cfd, resp, n);
}

static void dump_file_prefixed(int cfd, const char *tag, const char *path, off_t *last_pos)
{
	int fd = open(path, O_RDONLY);
	char buf[1024];
	int n;
	if (fd < 0)
		return;
	if (last_pos && *last_pos > 0)
		lseek(fd, *last_pos, SEEK_SET);
	while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
		buf[n] = '\0';
		if (last_pos)
			*last_pos += n;
		char *line = buf;
		while (line && *line) {
			char *nl = strchr(line, '\n');
			if (nl)
				*nl = '\0';
			if (*line) {
				char out[1152];
				int olen = snprintf(out, sizeof(out), "I/%-8s: %s\n", tag, line);
				if (write_all(cfd, out, olen) < 0) {
					close(fd);
					return;
				}
			}
			line = nl ? (nl + 1) : NULL;
		}
	}
	close(fd);
}

static void handle_logcat(int cfd, const char *arg)
{
	int follow = (arg && strcmp(arg, "follow") == 0);
	off_t vold_pos = 0, sadb_pos = 0;

	write_all(cfd, "OKAY\n", 5);

	/* 1. Dump kernel dmesg via /bin/dmesg */
	{
		int pfd[2];
		if (pipe(pfd) == 0) {
			pid_t pid = fork();
			if (pid == 0) {
				char *av[2] = { "dmesg", NULL };
				char *ev[2] = { "PATH=/bin:/sbin", NULL };
				close(pfd[0]);
				dup2(pfd[1], 1);
				dup2(pfd[1], 2);
				if (pfd[1] > 2)
					close(pfd[1]);
				execve("/bin/dmesg", av, ev);
				_exit(0);
			} else if (pid > 0) {
				char kbuf[1024];
				int r;
				close(pfd[1]);
				while ((r = read(pfd[0], kbuf, sizeof(kbuf))) > 0) {
					if (write_all(cfd, kbuf, r) < 0)
						break;
				}
				close(pfd[0]);
				waitpid(pid, NULL, 0);
			} else {
				close(pfd[0]);
				close(pfd[1]);
			}
		}
	}

	/* 2. Dump daemon logs */
	dump_file_prefixed(cfd, "vold", "/tmp/vold.log", &vold_pos);
	dump_file_prefixed(cfd, "sadbd", "/tmp/sadbd.log", &sadb_pos);

	while (follow) {
		fd_set rfds;
		struct timeval tv;
		FD_ZERO(&rfds);
		FD_SET(cfd, &rfds);
		tv.tv_sec = 0;
		tv.tv_usec = 250000;
		if (select(cfd + 1, &rfds, NULL, NULL, &tv) > 0) {
			char tmp[16];
			if (read(cfd, tmp, sizeof(tmp)) <= 0)
				break;
		}
		dump_file_prefixed(cfd, "vold", "/tmp/vold.log", &vold_pos);
		dump_file_prefixed(cfd, "sadbd", "/tmp/sadbd.log", &sadb_pos);
	}
}

static void handle_client(int cfd, const char *serial, int port)
{
	char line[2048];
	int len = read_line(cfd, line, sizeof(line));
	if (len <= 0)
		return;

	log_msg("sadbd: request '%s'\n", line);

	if (strcmp(line, "INFO") == 0 || strcmp(line, "PING") == 0) {
		handle_info(cfd, serial, port);
	} else if (strncmp(line, "EXEC ", 5) == 0) {
		handle_exec(cfd, line + 5);
	} else if (strcmp(line, "SHELL") == 0) {
		handle_shell(cfd, "");
	} else if (strncmp(line, "SHELL ", 6) == 0) {
		handle_shell(cfd, line + 6);
	} else if (strncmp(line, "PUSH ", 5) == 0) {
		handle_push(cfd, line + 5);
	} else if (strncmp(line, "PULL ", 5) == 0) {
		handle_pull(cfd, line + 5);
	} else if (strncmp(line, "STAT ", 5) == 0) {
		handle_stat(cfd, line + 5);
	} else if (strcmp(line, "LOGCAT") == 0) {
		handle_logcat(cfd, "dump");
	} else if (strncmp(line, "LOGCAT ", 7) == 0) {
		handle_logcat(cfd, line + 7);
	} else if (strncmp(line, "REBOOT", 6) == 0) {
		write_all(cfd, "OKAY\n", 5);
		sync();
		if (strstr(line, "halt") || strstr(line, "poweroff")) {
			char *av[2] = { "halt", NULL };
			char *ev[2] = { "PATH=/bin:/sbin", NULL };
			execve("/bin/halt", av, ev);
		}
	} else {
		const char *err = "FAIL unknown command\n";
		write_all(cfd, err, strlen(err));
	}
}

int main(int argc, char **argv)
{
	char serial[32] = "emulator-5554";
	int port = 5555;
	int ctl_fd;

	signal(SIGPIPE, SIG_IGN);

	ctl_fd = open("/dev/sadb", O_RDWR);
	if (ctl_fd < 0) {
		fprintf(stderr, "sadbd: cannot open /dev/sadb (errno=%d)\n", errno);
		return 1;
	}
	ioctl(ctl_fd, SADB_IOC_GET_SERIAL, (unsigned long)serial);
	port = ioctl(ctl_fd, SADB_IOC_GET_PORT, 0);
	close(ctl_fd);

	log_msg("sadbd: daemon started on /dev/sadb (%s)\n", serial);

	for (;;) {
		int cfd;
		int slot_id;
		pid_t pid;

		while (waitpid(-1, NULL, WNOHANG) > 0)
			;

		cfd = open("/dev/sadb", O_RDWR);
		if (cfd < 0) {
			sleep(1);
			continue;
		}

		for (;;) {
			while (waitpid(-1, NULL, WNOHANG) > 0)
				;
			slot_id = ioctl(cfd, SADB_IOC_ACCEPT, 0);
			if (slot_id >= 0)
				break;
			if (errno == EINTR)
				continue;
			close(cfd);
			cfd = -1;
			sleep(1);
			break;
		}

		if (cfd < 0)
			continue;

		pid = fork();
		if (pid < 0) {
			close(cfd);
			continue;
		}
		if (pid == 0) {
			handle_client(cfd, serial, port);
			close(cfd);
			_exit(0);
		}
		/* Parent closes its copy of cfd; child retains connection until exit */
		close(cfd);
	}
	return 0;
}
