/*
 * applications/sarthak/sarthak.c
 *
 * Split-Screen Dual-Shell Comparator CLI (/bin/sarthak, /bin/erofs_compare)
 * for comparing Stock EROFS + dm-verity (/bin on /dev/dm-0) against
 * Sarthak Kukreti's Native EROFS Verity (go/erofs-verity, /bin-sarthak on /dev/dm-7).
 *
 * Features:
 *   - Divides the terminal into two equal halves separated by a vertical '|' bar:
 *       Left Half  : [/bin]#         (Stock EROFS + dm-verity /dev/dm-0)
 *       Right Half : [/bin-sarthak]# (Native EROFS Verity go/erofs-verity /dev/dm-7)
 *   - Live per-command kernel telemetry (via /proc/erofs + monotonic clock):
 *       * Kernel do_execve() binary load + verification latency (us)
 *       * Userland end-to-end command turnaround time (us)
 *       * Data blocks read vs. Merkle tree blocks read (+3x/blk vs 0 inline)
 *       * SHA-256 calls and compression rounds (4 calls / 68 rounds vs 1 call / 16 rounds)
 *       * Metadata verified-bitmap cache hits (meta_verified_bitmap)
 *   - Interactive controls:
 *       * [Tab]       Switch active shell prompt between Left (/bin) and Right (/bin-sarthak)
 *       * [Ctrl+B]    Toggle Mirror Mode (broadcast typed command to both sides simultaneously)
 *       * [Ctrl+F]    Cycle Cache Mode: COLD (Data) -> COLD-ALL (Data+Meta) -> WARM
 *       * [Ctrl+L]    Clear scrollback
 *       * [Ctrl+D]    Exit (or type 'exit' / 'quit')
 *   - Non-interactive CLI modes:
 *       * sarthak --compare "<cmd>" / -c "<cmd>" : Run <cmd> on both sides & print split view
 *       * sarthak --demo                         : Run multi-binary benchmark & print split view
 *       * sarthak --status / -s                  : Show /proc/erofs mount & verity telemetry
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

extern int select(int nfds, fd_set *readfds, fd_set *writefds,
		  fd_set *exceptfds, struct timeval *timeout);

#define PANE_LEFT   0   /* /bin (Stock EROFS + dm-verity) */
#define PANE_RIGHT  1   /* /bin-sarthak (Native EROFS Verity go/erofs-verity) */

#define CACHE_COLD_DATA  0  /* Flush data pages/buffers before exec, keep metadata bitmap */
#define CACHE_COLD_ALL   1  /* Flush data + metadata bitmap before exec */
#define CACHE_WARM       2  /* Keep page/buffer cache warm */

#define MAX_LOG_LINES    64
#define MAX_LINE_LEN     128
#define MAX_CMD_LEN      128

struct mount_telemetry {
	char mount_path[32];
	char dev_path[32];
	char label[32];
	char mode[32];
	unsigned long blocks;
	unsigned long inos;
	unsigned long meta_blks;
	unsigned long merkle_blks;

	unsigned long execs;
	char last_comm[32];
	unsigned long last_bytes;
	unsigned long last_us;
	unsigned long last_data_blks;
	unsigned long last_merkle_blks;
	unsigned long last_sha256_calls;
	unsigned long last_sha256_rounds;
	unsigned long last_meta_hits;

	unsigned long total_data_blks;
	unsigned long total_merkle_blks;
	unsigned long total_sha256_calls;
	unsigned long total_sha256_rounds;
	unsigned long total_meta_hits;
	unsigned long meta_verified;
	unsigned long data_verified;
	unsigned long corrupt;
};

struct pane_state {
	const char *title;
	const char *bin_dir;
	const char *prompt;
	char cwd[128];
	char input[MAX_CMD_LEN];
	int input_len;
	char log[MAX_LOG_LINES][MAX_LINE_LEN];
	int log_count;
	struct mount_telemetry tele;
	unsigned long last_wall_us;
	int last_exit_code;
};

static struct pane_state panes[2];
static int active_pane = PANE_LEFT;
static int mirror_mode = 0;
static int cache_mode = CACHE_COLD_DATA;
static int term_rows = 24;
static int term_cols = 80;
static int raw_active = 0;
static struct termios saved_tio;
static volatile int win_resized = 0;

static void sigwinch_handler(int sig)
{
	(void)sig;
	win_resized = 1;
}

static unsigned long get_wall_us(void)
{
	struct timeval tv;
	struct timezone tz;
	memset(&tv, 0, sizeof(tv));
	memset(&tz, 0, sizeof(tz));
	gettimeofday(&tv, &tz);
	return (unsigned long)tv.tv_sec * 1000000UL + (unsigned long)tv.tv_usec;
}

static void write_erofs_ctl(const char *cmd)
{
	int fd = open("/proc/erofs", O_WRONLY);
	if (fd >= 0) {
		write(fd, cmd, strlen(cmd));
		close(fd);
	}
}

static unsigned long extract_ul(const char *line, const char *key)
{
	const char *p = strstr(line, key);
	if (!p)
		return 0;
	p += strlen(key);
	return strtoul(p, NULL, 10);
}

static void extract_str(const char *line, const char *key, char *out, int maxlen)
{
	const char *p = strstr(line, key);
	int i = 0;
	if (!p) {
		out[0] = '\0';
		return;
	}
	p += strlen(key);
	while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && i < maxlen - 1)
		out[i++] = *p++;
	out[i] = '\0';
}

static void refresh_telemetry(void)
{
	char buf[3072];
	int fd, n;
	char *line, *next;
	struct mount_telemetry *cur = NULL;

	fd = open("/proc/erofs", O_RDONLY);
	if (fd < 0)
		return;
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return;
	buf[n] = '\0';

	line = buf;
	while (line && *line) {
		next = strchr(line, '\n');
		if (next)
			*next++ = '\0';

		if (strncmp(line, "mount=", 6) == 0) {
			char mpath[32];
			extract_str(line, "mount=", mpath, sizeof(mpath));
			if (strcmp(mpath, "/bin") == 0)
				cur = &panes[PANE_LEFT].tele;
			else if (strcmp(mpath, "/bin-sarthak") == 0)
				cur = &panes[PANE_RIGHT].tele;
			else
				cur = NULL;

			if (cur) {
				strcpy(cur->mount_path, mpath);
				extract_str(line, "dev=", cur->dev_path, sizeof(cur->dev_path));
				extract_str(line, "label=", cur->label, sizeof(cur->label));
				extract_str(line, "mode=", cur->mode, sizeof(cur->mode));
				cur->blocks = extract_ul(line, "blocks=");
				cur->inos = extract_ul(line, "inos=");
				cur->meta_blks = extract_ul(line, "meta_blks=");
				cur->merkle_blks = extract_ul(line, "merkle_blks=");
			}
		} else if (cur && strstr(line, "execs=")) {
			cur->execs = extract_ul(line, "execs=");
			extract_str(line, "last_comm=", cur->last_comm, sizeof(cur->last_comm));
			cur->last_bytes = extract_ul(line, "last_bytes=");
			cur->last_us = extract_ul(line, "last_us=");
			cur->last_data_blks = extract_ul(line, "last_data_blks=");
			cur->last_merkle_blks = extract_ul(line, "last_merkle_blks=");
			cur->last_sha256_calls = extract_ul(line, "last_sha256_calls=");
			cur->last_sha256_rounds = extract_ul(line, "last_sha256_rounds=");
			cur->last_meta_hits = extract_ul(line, "last_meta_hits=");
		} else if (cur && strstr(line, "total_data_blks=")) {
			cur->total_data_blks = extract_ul(line, "total_data_blks=");
			cur->total_merkle_blks = extract_ul(line, "total_merkle_blks=");
			cur->total_sha256_calls = extract_ul(line, "total_sha256_calls=");
			cur->total_sha256_rounds = extract_ul(line, "total_sha256_rounds=");
			cur->total_meta_hits = extract_ul(line, "total_meta_hits=");
			cur->meta_verified = extract_ul(line, "meta_verified=");
			cur->data_verified = extract_ul(line, "data_verified=");
			cur->corrupt = extract_ul(line, "corrupt=");
		}
		line = next;
	}
}

static void pane_append_line(int side, const char *text)
{
	struct pane_state *p = &panes[side];
	int i;

	if (p->log_count >= MAX_LOG_LINES) {
		for (i = 1; i < MAX_LOG_LINES; i++)
			strcpy(p->log[i - 1], p->log[i]);
		p->log_count = MAX_LOG_LINES - 1;
	}
	strncpy(p->log[p->log_count], text, MAX_LINE_LEN - 1);
	p->log[p->log_count][MAX_LINE_LEN - 1] = '\0';
	p->log_count++;
}

static void pane_append_output(int side, const char *raw_out)
{
	char line[MAX_LINE_LEN];
	int idx = 0;
	const char *s = raw_out;

	while (*s) {
		if (*s == '\r') {
			s++;
			continue;
		}
		if (*s == '\n') {
			line[idx] = '\0';
			pane_append_line(side, line);
			idx = 0;
			s++;
			continue;
		}
		if (*s == '\t') {
			int spaces = 4 - (idx & 3);
			while (spaces-- > 0 && idx < MAX_LINE_LEN - 1)
				line[idx++] = ' ';
			s++;
			continue;
		}
		if ((unsigned char)*s >= 32 && idx < MAX_LINE_LEN - 1)
			line[idx++] = *s;
		s++;
	}
	if (idx > 0) {
		line[idx] = '\0';
		pane_append_line(side, line);
	}
}

static void update_winsize(void)
{
	struct winsize ws;
	if (ioctl(0, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 40 && ws.ws_row >= 12) {
		term_cols = ws.ws_col;
		term_rows = ws.ws_row;
	}
}

static void enable_raw_tty(void)
{
	struct termios raw;
	if (tcgetattr(0, &saved_tio) < 0)
		return;
	raw = saved_tio;
	raw.c_lflag &= ~(ICANON | ECHO | ISIG);
	raw.c_iflag &= ~(ICRNL | IXON);
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;
	tcsetattr(0, TCSANOW, &raw);
	raw_active = 1;
}

static void disable_raw_tty(void)
{
	if (raw_active) {
		tcsetattr(0, TCSANOW, &saved_tio);
		raw_active = 0;
	}
}

static const char *cache_mode_name(void)
{
	switch (cache_mode) {
	case CACHE_COLD_DATA: return "COLD (Data)";
	case CACHE_COLD_ALL:  return "COLD-ALL (Data+Meta)";
	default:              return "WARM (PageCache)";
	}
}

static int has_shell_meta(const char *cmd)
{
	const char *p;
	for (p = cmd; *p; p++) {
		if (*p == '|' || *p == '>' || *p == '<' || *p == ';' ||
		    *p == '&' || *p == '$' || *p == '`' || *p == '\'' || *p == '"')
			return 1;
	}
	return 0;
}

/*
 * Resolve a command token to the pane's binary directory (/bin or /bin-sarthak).
 * Even if the user types '/bin/ls' on the right pane, map it to '/bin-sarthak/ls'
 * unless they explicitly prefixed another directory like '/vendor/bin/...'.
 */
static void resolve_pane_binary(int side, const char *prog, char *out_path, int maxlen)
{
	const char *base = prog;
	struct stat st;

	if (strncmp(prog, "/bin/", 5) == 0)
		base = prog + 5;
	else if (strncmp(prog, "/bin-sarthak/", 13) == 0)
		base = prog + 13;
	else if (prog[0] == '/') {
		strncpy(out_path, prog, maxlen - 1);
		out_path[maxlen - 1] = '\0';
		return;
	}

	snprintf(out_path, maxlen, "%s/%s", panes[side].bin_dir, base);
	if (stat(out_path, &st) == 0)
		return;
	snprintf(out_path, maxlen, "/bin/%s", base);
}

static void execute_on_pane(int side, const char *cmdline)
{
	struct pane_state *p = &panes[side];
	char prompt_echo[MAX_LINE_LEN];
	char summary[MAX_LINE_LEN];
	char cmd_copy[MAX_CMD_LEN];
	char *trimmed;
	int pfd[2];
	int pid, status = 0;
	unsigned long t0, t1;

	strncpy(cmd_copy, cmdline, sizeof(cmd_copy) - 1);
	cmd_copy[sizeof(cmd_copy) - 1] = '\0';
	trimmed = cmd_copy;
	while (*trimmed == ' ' || *trimmed == '\t')
		trimmed++;
	if (!*trimmed)
		return;

	snprintf(prompt_echo, sizeof(prompt_echo), "%s%s", p->prompt, trimmed);
	pane_append_line(side, prompt_echo);

	/* Built-in commands */
	if (strcmp(trimmed, "clear") == 0) {
		p->log_count = 0;
		return;
	}
	if (strcmp(trimmed, "pwd") == 0) {
		pane_append_line(side, p->cwd);
		return;
	}
	if (strncmp(trimmed, "cd", 2) == 0 && (trimmed[2] == '\0' || trimmed[2] == ' ')) {
		const char *target = trimmed + 2;
		struct stat st;
		while (*target == ' ')
			target++;
		if (!*target)
			target = "/";
		if (stat((char *)target, &st) == 0 && S_ISDIR(st.st_mode)) {
			chdir(target);
			if (!getcwd(p->cwd, sizeof(p->cwd)))
				strncpy(p->cwd, target, sizeof(p->cwd) - 1);
		} else {
			pane_append_line(side, "cd: no such directory");
		}
		return;
	}
	if (strcmp(trimmed, "cold") == 0) {
		cache_mode = CACHE_COLD_DATA;
		pane_append_line(side, "[cache] Mode set to COLD (Data flushed before each exec)");
		return;
	}
	if (strcmp(trimmed, "cold_all") == 0) {
		cache_mode = CACHE_COLD_ALL;
		pane_append_line(side, "[cache] Mode set to COLD-ALL (Data + Metadata bitmap flushed)");
		return;
	}
	if (strcmp(trimmed, "warm") == 0) {
		cache_mode = CACHE_WARM;
		write_erofs_ctl("warm\n");
		pane_append_line(side, "[cache] Mode set to WARM (Page/Buffer cache retained)");
		return;
	}
	if (strcmp(trimmed, "mirror") == 0 || strcmp(trimmed, "both") == 0) {
		mirror_mode = !mirror_mode;
		pane_append_line(side, mirror_mode ?
			"[mirror] Mirror Mode ON (commands run on both /bin and /bin-sarthak)" :
			"[mirror] Mirror Mode OFF (commands run on active pane only)");
		return;
	}
	if (strcmp(trimmed, "help") == 0) {
		pane_append_line(side, "Builtins: cd, pwd, clear, cold, cold_all, warm, mirror, exit");
		pane_append_line(side, "Keys: [Tab] Switch Pane  [Ctrl+B] Mirror  [Ctrl+F] Cache Mode");
		return;
	}

	if (pipe(pfd) < 0) {
		pane_append_line(side, "error: pipe() failed");
		return;
	}

	if (cache_mode == CACHE_COLD_DATA)
		write_erofs_ctl("cold\n");
	else if (cache_mode == CACHE_COLD_ALL)
		write_erofs_ctl("cold_all\n");
	else
		write_erofs_ctl("warm\n");

	t0 = get_wall_us();
	pid = fork();
	if (pid == 0) {
		char path_env[128];
		char *envp[6];
		char *argv[16];
		char exec_path[128];
		int argc = 0;

		close(pfd[0]);
		dup2(pfd[1], 1);
		dup2(pfd[1], 2);
		if (pfd[1] > 2)
			close(pfd[1]);

		chdir(p->cwd);
		if (side == PANE_LEFT)
			strcpy(path_env, "PATH=/bin:/usr/bin:/vendor/bin");
		else
			strcpy(path_env, "PATH=/bin-sarthak:/bin:/usr/bin:/vendor/bin");

		envp[0] = path_env;
		envp[1] = "HOME=/";
		envp[2] = "TERM=vt100";
		envp[3] = "USER=root";
		envp[4] = NULL;

		if (!has_shell_meta(trimmed)) {
			char *tok = strtok(trimmed, " \t");
			while (tok && argc < 15) {
				argv[argc++] = tok;
				tok = strtok(NULL, " \t");
			}
			argv[argc] = NULL;
			if (argc > 0) {
				resolve_pane_binary(side, argv[0], exec_path, sizeof(exec_path));
				argv[0] = exec_path;
				execve(exec_path, argv, envp);
			}
		} else {
			char rewritten[MAX_CMD_LEN + 32];
			char first_tok[64];
			const char *rest = trimmed;
			int k = 0;
			while (*rest && *rest != ' ' && *rest != '\t' && k < (int)sizeof(first_tok) - 1)
				first_tok[k++] = *rest++;
			first_tok[k] = '\0';
			resolve_pane_binary(side, first_tok, exec_path, sizeof(exec_path));
			snprintf(rewritten, sizeof(rewritten), "%s%s", exec_path, rest);
			argv[0] = "/bin/sh";
			argv[1] = "-c";
			argv[2] = rewritten;
			argv[3] = NULL;
			execve("/bin/sh", argv, envp);
		}
		printf("exec failed: %s (errno=%d)\n", trimmed, errno);
		_exit(127);
	}

	close(pfd[1]);
	if (pid > 0) {
		char outbuf[2048];
		int total = 0, n;
		while (total < (int)sizeof(outbuf) - 1 &&
		       (n = read(pfd[0], outbuf + total, sizeof(outbuf) - 1 - total)) > 0) {
			total += n;
		}
		outbuf[total] = '\0';
		close(pfd[0]);
		waitpid(pid, &status, 0);
		t1 = get_wall_us();
		p->last_wall_us = (t1 > t0) ? (t1 - t0) : 1;
		p->last_exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
		if (total > 0)
			pane_append_output(side, outbuf);
	} else {
		close(pfd[0]);
		pane_append_line(side, "error: fork() failed");
	}

	refresh_telemetry();
	snprintf(summary, sizeof(summary),
		 "=> %luus %lu+%lum %luc/%lur hit:%lu",
		 p->tele.last_us,
		 p->tele.last_data_blks,
		 p->tele.last_merkle_blks,
		 p->tele.last_sha256_calls,
		 p->tele.last_sha256_rounds,
		 p->tele.last_meta_hits);
	pane_append_line(side, summary);
}

static void print_padded(const char *str, int width)
{
	int len = (int)strlen(str);
	int i;
	for (i = 0; i < width; i++) {
		if (i < len)
			putchar(str[i]);
		else
			putchar(' ');
	}
}

/*
 * Format speedup ratio (e.g. "1.82x") using integer arithmetic.
 */
static void format_ratio(unsigned long num, unsigned long den, char *out, int maxlen)
{
	unsigned long x100;
	if (den == 0 || num == 0) {
		snprintf(out, maxlen, "1.00x");
		return;
	}
	x100 = (num * 100UL + (den / 2)) / den;
	snprintf(out, maxlen, "%lu.%02lux", x100 / 100UL, x100 % 100UL);
}

static char frame_buf[32768];
static int frame_len = 0;

static void fb_flush(void)
{
	int off = 0;
	while (off < frame_len) {
		int n = write(1, frame_buf + off, frame_len - off);
		if (n <= 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		off += n;
	}
	frame_len = 0;
}

static void fb_reset(void)
{
	frame_len = 0;
}

static void fb_puts(const char *s)
{
	int n = (int)strlen(s);
	if (frame_len + n >= (int)sizeof(frame_buf))
		fb_flush();
	if (frame_len + n < (int)sizeof(frame_buf)) {
		memcpy(frame_buf + frame_len, s, n);
		frame_len += n;
	}
}

static void fb_putc(char c)
{
	if (frame_len + 1 >= (int)sizeof(frame_buf))
		fb_flush();
	if (frame_len + 1 < (int)sizeof(frame_buf))
		frame_buf[frame_len++] = c;
}

static void fb_repeat(char c, int count)
{
	int i;
	for (i = 0; i < count; i++)
		fb_putc(c);
}

static void fb_padded(const char *str, int width)
{
	int len = (int)strlen(str);
	int i;
	for (i = 0; i < width; i++)
		fb_putc((i < len) ? str[i] : ' ');
}

static void compute_layout(int *left_w_out, int *right_w_out, int *log_rows_out)
{
	int lw, rw, lr;
	update_winsize();
	lw = (term_cols - 3) / 2;
	rw = term_cols - 3 - lw;
	if (lw < 20)
		lw = 20;
	if (rw < 20)
		rw = 20;
	lr = term_rows - 11;
	if (lr < 4)
		lr = 4;
	*left_w_out = lw;
	*right_w_out = rw;
	*log_rows_out = lr;
}

static void append_prompt_row_to_fb(int left_w, int right_w)
{
	struct pane_state *L = &panes[PANE_LEFT];
	struct pane_state *R = &panes[PANE_RIGHT];
	const char *l_in = mirror_mode ? panes[active_pane].input : L->input;
	const char *r_in = mirror_mode ? panes[active_pane].input : R->input;
	char lbuf[160], rbuf[160];

	snprintf(lbuf, sizeof(lbuf), "%s%s%s",
		 L->prompt, l_in,
		 (active_pane == PANE_LEFT || mirror_mode) ? "_" : "");
	snprintf(rbuf, sizeof(rbuf), "%s%s%s",
		 R->prompt, r_in,
		 (active_pane == PANE_RIGHT || mirror_mode) ? "_" : "");

	fb_puts("\033[30m");
	fb_padded(lbuf, left_w);
	fb_puts("\033[0m | \033[30m");
	fb_padded(rbuf, right_w);
	fb_puts("\033[0m");
}

/*
 * Fast-path incremental prompt update: redraws ONLY the single prompt row
 * in-place with a single ~100-byte write(1, ...) without touching or clearing
 * the rest of the screen.
 */
static void render_prompt_row(void)
{
	int left_w, right_w, log_rows, prompt_row;
	char pos_seq[32];

	compute_layout(&left_w, &right_w, &log_rows);
	prompt_row = 9 + log_rows;

	fb_reset();
	snprintf(pos_seq, sizeof(pos_seq), "\033[?25l\033[%d;1H", prompt_row);
	fb_puts(pos_seq);
	append_prompt_row_to_fb(left_w, right_w);
	fb_puts("\033[?25h");
	fb_flush();
}

static void render_split_screen_ex(int clear_first)
{
	int left_w, right_w;
	int log_rows, r;
	struct pane_state *L = &panes[PANE_LEFT];
	struct pane_state *R = &panes[PANE_RIGHT];
	char lbuf[160], rbuf[160];
	char speedup_str[24], sha_ratio_str[24];
	int same_comm = 0;

	compute_layout(&left_w, &right_w, &log_rows);

	if (L->tele.last_comm[0] && L->tele.last_comm[0] != '-' &&
	    strcmp(L->tele.last_comm, R->tele.last_comm) == 0 &&
	    R->tele.last_us > 0) {
		same_comm = 1;
		format_ratio(L->tele.last_us, R->tele.last_us, speedup_str, sizeof(speedup_str));
		format_ratio(L->tele.last_sha256_rounds, R->tele.last_sha256_rounds,
			     sha_ratio_str, sizeof(sha_ratio_str));
	} else {
		strcpy(speedup_str, "-");
		strcpy(sha_ratio_str, "-");
	}

	fb_reset();
	if (clear_first)
		fb_puts("\033[?25l\033[H\033[2J");
	else
		fb_puts("\033[?25l\033[H");

	/* Row 1: Top Title Banner */
	fb_puts("\033[1;37;44m");
	snprintf(lbuf, sizeof(lbuf),
		 " SIX EROFS VERITY COMPARATOR | Cache: %-20s | Mirror: %-3s",
		 cache_mode_name(), mirror_mode ? "ON" : "OFF");
	fb_padded(lbuf, term_cols);
	fb_puts("\033[0m\n");

	/* Row 2: Pane Headers */
	if (active_pane == PANE_LEFT || mirror_mode)
		fb_puts("\033[1;30;42m");
	else
		fb_puts("\033[1;37;40m");
	snprintf(lbuf, sizeof(lbuf), " [/bin] Stock EROFS + dm-verity");
	fb_padded(lbuf, left_w);
	fb_puts("\033[0m | ");

	if (active_pane == PANE_RIGHT || mirror_mode)
		fb_puts("\033[1;30;46m");
	else
		fb_puts("\033[1;37;40m");
	snprintf(rbuf, sizeof(rbuf), " [/bin-sarthak] go/erofs-verity");
	fb_padded(rbuf, right_w);
	fb_puts("\033[0m\n");

	/* Row 3: Telemetry Line 1 - Last Exec & Load Time */
	snprintf(lbuf, sizeof(lbuf), " Exec:%-9s (%5luB) Load:%5luus",
		 L->tele.last_comm[0] ? L->tele.last_comm : "-",
		 L->tele.last_bytes, L->tele.last_us);
	if (same_comm) {
		snprintf(rbuf, sizeof(rbuf), " Exec:%-9s (%5luB) %5luus %s",
			 R->tele.last_comm, R->tele.last_bytes, R->tele.last_us, speedup_str);
	} else {
		snprintf(rbuf, sizeof(rbuf), " Exec:%-9s (%5luB) Load:%5luus",
			 R->tele.last_comm[0] ? R->tele.last_comm : "-",
			 R->tele.last_bytes, R->tele.last_us);
	}
	fb_puts("\033[30m");
	fb_padded(lbuf, left_w);
	fb_puts("\033[0m | \033[30m");
	fb_padded(rbuf, right_w);
	fb_puts("\033[0m\n");

	/* Row 4: Telemetry Line 2 - Block I/O & Merkle Overhead */
	snprintf(lbuf, sizeof(lbuf), " Blks: %2lu data + %3lu Merkle (+3x tree)",
		 L->tele.last_data_blks, L->tele.last_merkle_blks);
	snprintf(rbuf, sizeof(rbuf), " Blks: %2lu data + %3lu Merkle (inline)",
		 R->tele.last_data_blks, R->tele.last_merkle_blks);
	fb_padded(lbuf, left_w);
	fb_puts(" | ");
	fb_padded(rbuf, right_w);
	fb_putc('\n');

	/* Row 5: Telemetry Line 3 - SHA-256 Calls & Compression Rounds */
	snprintf(lbuf, sizeof(lbuf), " SHA : %3lu calls / %4lu rounds (68r/b)",
		 L->tele.last_sha256_calls, L->tele.last_sha256_rounds);
	snprintf(rbuf, sizeof(rbuf), " SHA : %3lu calls / %4lu rounds (16r/b)",
		 R->tele.last_sha256_calls, R->tele.last_sha256_rounds);
	fb_padded(lbuf, left_w);
	fb_puts(" | ");
	fb_padded(rbuf, right_w);
	fb_putc('\n');

	/* Row 6: Telemetry Line 4 - Metadata Bitmap Hits & Totals */
	snprintf(lbuf, sizeof(lbuf), " Meta: 0 bitmap hits (Tot:%lur SHA)",
		 L->tele.total_sha256_rounds);
	snprintf(rbuf, sizeof(rbuf), " Meta: %2lu bitmap hits (%lu/%lu ver)",
		 R->tele.last_meta_hits, R->tele.meta_verified, R->tele.meta_blks);
	fb_padded(lbuf, left_w);
	fb_puts(" | ");
	fb_padded(rbuf, right_w);
	fb_putc('\n');

	/* Row 7: Horizontal Divider */
	fb_repeat('-', left_w);
	fb_puts("-+-");
	fb_repeat('-', right_w);
	fb_putc('\n');

	/* Middle Rows: Dual Shell Scrollback Output */
	for (r = 0; r < log_rows; r++) {
		int l_idx = (L->log_count > log_rows) ? (L->log_count - log_rows + r) : r;
		int r_idx = (R->log_count > log_rows) ? (R->log_count - log_rows + r) : r;
		const char *l_txt = (l_idx < L->log_count) ? L->log[l_idx] : "";
		const char *r_txt = (r_idx < R->log_count) ? R->log[r_idx] : "";

		fb_puts("\033[30m");
		fb_padded(l_txt, left_w);
		fb_puts("\033[0m | \033[30m");
		fb_padded(r_txt, right_w);
		fb_puts("\033[0m\n");
	}

	/* Bottom Divider */
	fb_repeat('-', left_w);
	fb_puts("-+-");
	fb_repeat('-', right_w);
	fb_putc('\n');

	/* Interactive Dual Shell Prompt Row */
	append_prompt_row_to_fb(left_w, right_w);
	fb_putc('\n');

	/* Head-to-Head Comparison Bar */
	fb_puts("\033[1;37;40m");
	if (same_comm) {
		snprintf(lbuf, sizeof(lbuf),
			 " VS [%s]: /bin %luus (%lu+%lum,%lur) vs /bin-sarthak %luus (%lu+0m,%lur) => %s faster (%s SHA)",
			 R->tele.last_comm,
			 L->tele.last_us, L->tele.last_data_blks, L->tele.last_merkle_blks, L->tele.last_sha256_rounds,
			 R->tele.last_us, R->tele.last_data_blks, R->tele.last_sha256_rounds,
			 speedup_str, sha_ratio_str);
	} else {
		snprintf(lbuf, sizeof(lbuf),
			 " Tip: Run the same command on both sides (or Ctrl+B Mirror Mode) to compare!");
	}
	fb_padded(lbuf, term_cols);
	fb_puts("\033[0m\n");

	/* Footer Keybindings Bar */
	fb_puts("\033[1;37;44m");
	snprintf(lbuf, sizeof(lbuf),
		 " [Tab] Side  [Ctrl+B] Mirror:%s  [Ctrl+F] Cache  [Ctrl+L] Clear  [Ctrl+X/exit] Quit",
		 mirror_mode ? "ON " : "OFF");
	fb_padded(lbuf, term_cols);
	fb_puts("\033[0m\033[?25h");
	fb_flush();
}

static void render_split_screen(void)
{
	render_split_screen_ex(0);
}

/*
 * Non-interactive side-by-side split view printer for --compare and --demo modes.
 */
static void print_batch_split_box(const char *cmd_label)
{
	int left_w = 38, right_w = 38;
	struct pane_state *L = &panes[PANE_LEFT];
	struct pane_state *R = &panes[PANE_RIGHT];
	char lbuf[128], rbuf[128], speedup[24], sha_ratio[24];
	int i, max_lines;

	format_ratio(L->tele.last_us, R->tele.last_us, speedup, sizeof(speedup));
	format_ratio(L->tele.last_sha256_rounds, R->tele.last_sha256_rounds, sha_ratio, sizeof(sha_ratio));

	printf("===============================================================================\n");
	snprintf(lbuf, sizeof(lbuf), "[/bin] Stock EROFS + dm-verity");
	snprintf(rbuf, sizeof(rbuf), "[/bin-sarthak] go/erofs-verity");
	print_padded(lbuf, left_w);
	printf(" | ");
	print_padded(rbuf, right_w);
	printf("\n");

	for (i = 0; i < left_w; i++) putchar('-');
	printf("-+-");
	for (i = 0; i < right_w; i++) putchar('-');
	printf("\n");

	snprintf(lbuf, sizeof(lbuf), "Binary : /bin/%s (%lu B)",
		 L->tele.last_comm, L->tele.last_bytes);
	snprintf(rbuf, sizeof(rbuf), "Binary : /bin-sarthak/%s (%lu B)",
		 R->tele.last_comm, R->tele.last_bytes);
	print_padded(lbuf, left_w);
	printf(" | ");
	print_padded(rbuf, right_w);
	printf("\n");

	snprintf(lbuf, sizeof(lbuf), "Load   : %lu us (wall %lu us)",
		 L->tele.last_us, L->last_wall_us);
	snprintf(rbuf, sizeof(rbuf), "Load   : %lu us (%s faster)",
		 R->tele.last_us, speedup);
	print_padded(lbuf, left_w);
	printf(" | ");
	print_padded(rbuf, right_w);
	printf("\n");

	snprintf(lbuf, sizeof(lbuf), "Blocks : %lu data + %lu Merkle (+3x)",
		 L->tele.last_data_blks, L->tele.last_merkle_blks);
	snprintf(rbuf, sizeof(rbuf), "Blocks : %lu data + %lu Merkle (inline)",
		 R->tele.last_data_blks, R->tele.last_merkle_blks);
	print_padded(lbuf, left_w);
	printf(" | ");
	print_padded(rbuf, right_w);
	printf("\n");

	snprintf(lbuf, sizeof(lbuf), "SHA256 : %lu calls / %lu rounds",
		 L->tele.last_sha256_calls, L->tele.last_sha256_rounds);
	snprintf(rbuf, sizeof(rbuf), "SHA256 : %lu calls / %lu rounds (%s)",
		 R->tele.last_sha256_calls, R->tele.last_sha256_rounds, sha_ratio);
	print_padded(lbuf, left_w);
	printf(" | ");
	print_padded(rbuf, right_w);
	printf("\n");

	snprintf(lbuf, sizeof(lbuf), "MetaHit: 0 bitmap hits");
	snprintf(rbuf, sizeof(rbuf), "MetaHit: %lu bitmap hits (%lu/%lu ver)",
		 R->tele.last_meta_hits, R->tele.meta_verified, R->tele.meta_blks);
	print_padded(lbuf, left_w);
	printf(" | ");
	print_padded(rbuf, right_w);
	printf("\n");

	for (i = 0; i < left_w; i++) putchar('-');
	printf("-+-");
	for (i = 0; i < right_w; i++) putchar('-');
	printf("\n");

	max_lines = (L->log_count > R->log_count) ? L->log_count : R->log_count;
	for (i = 0; i < max_lines; i++) {
		const char *lt = (i < L->log_count) ? L->log[i] : "";
		const char *rt = (i < R->log_count) ? R->log[i] : "";
		print_padded(lt, left_w);
		printf(" | ");
		print_padded(rt, right_w);
		printf("\n");
	}

	printf("===============================================================================\n");
	printf("[SUMMARY %s] /bin: %lu us (%lu+%lu blks, %lu SHA rounds) vs /bin-sarthak: %lu us (%lu+%lu blks, %lu SHA rounds, %lu meta hits) -> %s load speedup, %s fewer SHA-256 rounds\n",
	       cmd_label,
	       L->tele.last_us, L->tele.last_data_blks, L->tele.last_merkle_blks, L->tele.last_sha256_rounds,
	       R->tele.last_us, R->tele.last_data_blks, R->tele.last_merkle_blks, R->tele.last_sha256_rounds,
	       R->tele.last_meta_hits,
	       speedup, sha_ratio);
}

static void init_panes(void)
{
	memset(panes, 0, sizeof(panes));

	panes[PANE_LEFT].title = "[/bin] Stock EROFS + dm-verity";
	panes[PANE_LEFT].bin_dir = "/bin";
	panes[PANE_LEFT].prompt = "[/bin]# ";
	if (!getcwd(panes[PANE_LEFT].cwd, sizeof(panes[PANE_LEFT].cwd)))
		strcpy(panes[PANE_LEFT].cwd, "/");

	panes[PANE_RIGHT].title = "[/bin-sarthak] Native EROFS Verity";
	panes[PANE_RIGHT].bin_dir = "/bin-sarthak";
	panes[PANE_RIGHT].prompt = "[/bin-sarthak]# ";
	strcpy(panes[PANE_RIGHT].cwd, panes[PANE_LEFT].cwd);

	refresh_telemetry();
	pane_append_line(PANE_LEFT, "Stock EROFS + dm-verity (/dev/dm-0)");
	pane_append_line(PANE_LEFT, "Type any cmd (ls, uname -a, dhrystone)");
	pane_append_line(PANE_RIGHT, "Native EROFS Verity (/dev/dm-7)");
	pane_append_line(PANE_RIGHT, "Inline 32B/blk + pinned meta Merkle");
}

static int run_demo_suite(void)
{
	static const char *demo_cmds[] = {
		"uname -a",
		"id",
		"ls /etc/selinux",
		"dhrystone",
		NULL
	};
	int i;

	init_panes();
	cache_mode = CACHE_COLD_DATA;
	printf("=== Sarthak Native EROFS Verity (go/erofs-verity) vs Stock EROFS + dm-verity ===\n");
	for (i = 0; demo_cmds[i]; i++) {
		panes[PANE_LEFT].log_count = 0;
		panes[PANE_RIGHT].log_count = 0;
		execute_on_pane(PANE_LEFT, demo_cmds[i]);
		execute_on_pane(PANE_RIGHT, demo_cmds[i]);
		print_batch_split_box(demo_cmds[i]);
	}
	return 0;
}

int main(int argc, char *argv[])
{
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--status") == 0 || strcmp(argv[i], "-s") == 0) {
			char buf[3072];
			int fd = open("/proc/erofs", O_RDONLY);
			if (fd >= 0) {
				int n = read(fd, buf, sizeof(buf) - 1);
				close(fd);
				if (n > 0) {
					buf[n] = '\0';
					printf("%s", buf);
				}
			}
			return 0;
		}
		if (strcmp(argv[i], "--demo") == 0) {
			return run_demo_suite();
		}
		if ((strcmp(argv[i], "--compare") == 0 || strcmp(argv[i], "-c") == 0) && i + 1 < argc) {
			init_panes();
			panes[PANE_LEFT].log_count = 0;
			panes[PANE_RIGHT].log_count = 0;
			execute_on_pane(PANE_LEFT, argv[i + 1]);
			execute_on_pane(PANE_RIGHT, argv[i + 1]);
			print_batch_split_box(argv[i + 1]);
			return 0;
		}
		if (strcmp(argv[i], "--mirror") == 0 || strcmp(argv[i], "-m") == 0) {
			mirror_mode = 1;
			continue;
		}
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			printf("Usage: sarthak [options]\n"
			       "  (no args)          Launch interactive split-screen dual-shell UI\n"
			       "  -m, --mirror       Launch interactive UI with Mirror Mode ON\n"
			       "  -c, --compare CMD  Run CMD on /bin and /bin-sarthak and compare side-by-side\n"
			       "  --demo             Run multi-binary head-to-head benchmark suite\n"
			       "  -s, --status       Print /proc/erofs telemetry\n");
			return 0;
		}
	}

	signal(SIGWINCH, sigwinch_handler);
	init_panes();
	enable_raw_tty();
	render_split_screen_ex(1);

	while (1) {
		unsigned char ch;
		int n;
		struct pane_state *cur = &panes[active_pane];

		if (win_resized) {
			win_resized = 0;
			render_split_screen_ex(1);
		}

		n = read(0, &ch, 1);
		if (n <= 0)
			break;

		if (ch == 4 || ch == 17) { /* Ctrl+D (empty line) or Ctrl+Q: exit */
			if (ch == 17 || cur->input_len == 0)
				break;
			continue;
		}
		if (ch == 24) { /* Ctrl+X: kill current input line if non-empty, else exit */
			if (cur->input_len > 0) {
				cur->input_len = 0;
				cur->input[0] = '\0';
				render_prompt_row();
				continue;
			}
			break;
		}
		if (ch == 3 || ch == 21) { /* Ctrl+C / Ctrl+U: clear current input line */
			cur->input_len = 0;
			cur->input[0] = '\0';
			render_prompt_row();
			continue;
		}
		if (ch == 23) { /* Ctrl+W: erase previous word */
			while (cur->input_len > 0 && cur->input[cur->input_len - 1] == ' ')
				cur->input[--cur->input_len] = '\0';
			while (cur->input_len > 0 && cur->input[cur->input_len - 1] != ' ')
				cur->input[--cur->input_len] = '\0';
			render_prompt_row();
			continue;
		}
		if (ch == '\t') { /* Tab: switch between Left (/bin) and Right (/bin-sarthak) */
			active_pane = (active_pane == PANE_LEFT) ? PANE_RIGHT : PANE_LEFT;
			render_split_screen();
			continue;
		}
		if (ch == 2 || ch == 20) { /* Ctrl+B or Ctrl+T: toggle Mirror Mode (both panes) */
			mirror_mode = !mirror_mode;
			render_split_screen();
			continue;
		}
		if (ch == 6) { /* Ctrl+F: cycle Cold / Cold-All / Warm cache mode */
			cache_mode = (cache_mode + 1) % 3;
			if (cache_mode == CACHE_WARM)
				write_erofs_ctl("warm\n");
			render_split_screen();
			continue;
		}
		if (ch == 12) { /* Ctrl+L: clear both panes */
			panes[PANE_LEFT].log_count = 0;
			panes[PANE_RIGHT].log_count = 0;
			render_split_screen_ex(1);
			continue;
		}
		if (ch == 27) { /* Escape sequence (arrow keys) */
			unsigned char seq[2];
			struct timeval tv = { 0, 20000 };
			fd_set rfds;
			FD_ZERO(&rfds);
			FD_SET(0, &rfds);
			if (select(1, &rfds, NULL, NULL, &tv) > 0 && read(0, &seq[0], 1) == 1) {
				if (seq[0] == '[' && read(0, &seq[1], 1) == 1) {
					if (seq[1] == 'D') { /* Left arrow -> Left pane */
						active_pane = PANE_LEFT;
						render_split_screen();
					} else if (seq[1] == 'C') { /* Right arrow -> Right pane */
						active_pane = PANE_RIGHT;
						render_split_screen();
					}
				}
			}
			continue;
		}
		if (ch == 127 || ch == 8) { /* Backspace */
			if (cur->input_len > 0) {
				cur->input[--cur->input_len] = '\0';
				render_prompt_row();
			}
			continue;
		}
		if (ch == '\r' || ch == '\n') {
			char cmd_to_run[MAX_CMD_LEN];
			strcpy(cmd_to_run, cur->input);
			cur->input_len = 0;
			cur->input[0] = '\0';

			if (strcmp(cmd_to_run, "exit") == 0 || strcmp(cmd_to_run, "quit") == 0)
				break;

			if (cmd_to_run[0] != '\0') {
				if (mirror_mode) {
					execute_on_pane(PANE_LEFT, cmd_to_run);
					execute_on_pane(PANE_RIGHT, cmd_to_run);
				} else {
					execute_on_pane(active_pane, cmd_to_run);
				}
			}
			render_split_screen();
			continue;
		}
		if (ch >= 32 && ch < 127) {
			if (cur->input_len < MAX_CMD_LEN - 1) {
				cur->input[cur->input_len++] = (char)ch;
				cur->input[cur->input_len] = '\0';
				render_prompt_row();
			}
		}
	}

	disable_raw_tty();
	printf("\033[%d;1H\033[?25h\033[0m\n", term_rows);
	fflush(stdout);
	return 0;
}
