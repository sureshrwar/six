/*
 * more.c - Terminal pager for SIX
 *
 * Displays files or standard input one screenful at a time.
 * Keys:
 *   Space     : Advance one screen
 *   Enter / j : Advance one line
 *   d         : Advance half screen
 *   b / k     : Back one screen (on seekable files)
 *   /pattern  : Search forward for pattern
 *   n         : Repeat last search
 *   h / ?     : Help
 *   q / Q     : Quit
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/termios.h>

#define DEFAULT_ROWS 24
#define DEFAULT_COLS 80
#define MAX_PAGE_STACK 4096

static int term_rows = DEFAULT_ROWS;
static int term_cols = DEFAULT_COLS;
static int is_tty = 0;
static int tty_in = -1;
static struct termios orig_termios;
static int raw_mode_active = 0;
static char last_search[128] = "";

static void restore_terminal(void)
{
	if (raw_mode_active && tty_in >= 0) {
		tcsetattr(tty_in, TCSANOW, &orig_termios);
		raw_mode_active = 0;
	}
}

static void sig_handler(int sig)
{
	restore_terminal();
	write(1, "\n", 1);
	exit(0);
}

static void enable_raw_mode(void)
{
	struct termios raw;
	if (!is_tty || tty_in < 0)
		return;

	if (tcgetattr(tty_in, &orig_termios) < 0)
		return;

	raw = orig_termios;
	raw.c_lflag &= ~(ICANON | ECHO);
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;

	if (tcsetattr(tty_in, TCSANOW, &raw) == 0) {
		raw_mode_active = 1;
		signal(SIGINT, sig_handler);
		signal(SIGTERM, sig_handler);
	}
}

static void get_window_size(void)
{
	struct winsize ws;
	if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) {
		term_rows = ws.ws_row;
		term_cols = ws.ws_col;
	} else {
		char *lines = getenv("LINES");
		char *cols = getenv("COLUMNS");
		term_rows = lines ? atoi(lines) : DEFAULT_ROWS;
		term_cols = cols ? atoi(cols) : DEFAULT_COLS;
	}
	if (term_rows < 4) term_rows = 4;
	if (term_cols < 20) term_cols = 20;
}

static int read_key(void)
{
	char c = 0;
	int fd = (tty_in >= 0) ? tty_in : 0;
	if (read(fd, &c, 1) <= 0)
		return 'q';
	return (unsigned char)c;
}

static void clear_prompt(int len)
{
	int i;
	putchar('\r');
	for (i = 0; i < len + 20; i++)
		putchar(' ');
	putchar('\r');
	fflush(stdout);
}

static void show_help(void)
{
	printf("\n--- more commands ---\n");
	printf("  <space>       Display next screen\n");
	printf("  <return>, j   Display next line\n");
	printf("  d             Display next half screen\n");
	printf("  b, k          Display previous screen\n");
	printf("  /string       Search forward for string\n");
	printf("  n             Repeat previous search\n");
	printf("  q, Q          Exit more\n");
	printf("  h, ?          Display this help\n");
	printf("---------------------\n");
	fflush(stdout);
}

static void view_stream(FILE *fp, const char *filename, off_t total_size, int seekable)
{
	char line[1024];
	off_t page_offsets[MAX_PAGE_STACK];
	int page_top = 0;
	int lines_to_show = term_rows - 1;
	int lines_printed = 0;

	if (seekable) {
		page_offsets[0] = ftell(fp);
	}

	while (1) {
		if (lines_to_show > 0) {
			if (!fgets(line, sizeof(line), fp)) {
				break; /* EOF */
			}
			fputs(line, stdout);
			lines_printed++;
			lines_to_show--;
			continue;
		}

		/* Reached end of screenful */
		if (!is_tty) {
			/* Not on a terminal, continuous output */
			lines_to_show = term_rows - 1;
			continue;
		}

		/* Display prompt */
		int prompt_len = 0;
		if (total_size > 0 && seekable) {
			off_t cur = ftell(fp);
			int pct = (int)((cur * 100) / total_size);
			if (pct > 100) pct = 100;
			prompt_len = printf("\033[7m--More--(%d%%)\033[0m", pct);
		} else if (filename) {
			prompt_len = printf("\033[7m--More--(%s)\033[0m", filename);
		} else {
			prompt_len = printf("\033[7m--More--\033[0m");
		}
		fflush(stdout);

		int cmd = read_key();
		clear_prompt(prompt_len);

		if (cmd == 'q' || cmd == 'Q') {
			break;
		} else if (cmd == ' ' || cmd == 4) { /* Space or Ctrl-D */
			lines_to_show = term_rows - 1;
			if (seekable && page_top < MAX_PAGE_STACK - 1) {
				page_offsets[++page_top] = ftell(fp);
			}
		} else if (cmd == '\n' || cmd == '\r' || cmd == 'j') {
			lines_to_show = 1;
		} else if (cmd == 'd') {
			lines_to_show = (term_rows - 1) / 2;
		} else if ((cmd == 'b' || cmd == 'k') && seekable) {
			if (page_top > 0) {
				page_top--;
				fseek(fp, page_offsets[page_top], SEEK_SET);
				lines_to_show = term_rows - 1;
			} else {
				fseek(fp, 0, SEEK_SET);
				lines_to_show = term_rows - 1;
			}
		} else if (cmd == '/') {
			/* Read search string */
			char query[128];
			int qlen = 0;
			printf("/");
			fflush(stdout);
			restore_terminal();
			if (fgets(query, sizeof(query), stdin >= 0 && !isatty(0) ? fdopen(tty_in, "r") : stdin)) {
				char *nl = strchr(query, '\n');
				if (nl) *nl = '\0';
				if (query[0]) {
					strncpy(last_search, query, sizeof(last_search) - 1);
				}
			}
			enable_raw_mode();
			if (last_search[0]) {
				int found = 0;
				while (fgets(line, sizeof(line), fp)) {
					if (strstr(line, last_search)) {
						fputs(line, stdout);
						lines_to_show = term_rows - 2;
						found = 1;
						break;
					}
				}
				if (!found) {
					printf("\033[7mPattern not found\033[0m\n");
					lines_to_show = 0;
				}
			}
		} else if (cmd == 'n') {
			if (last_search[0]) {
				int found = 0;
				while (fgets(line, sizeof(line), fp)) {
					if (strstr(line, last_search)) {
						fputs(line, stdout);
						lines_to_show = term_rows - 2;
						found = 1;
						break;
					}
				}
				if (!found) {
					printf("\033[7mPattern not found\033[0m\n");
					lines_to_show = 0;
				}
			}
		} else if (cmd == 'h' || cmd == '?') {
			show_help();
			lines_to_show = 0;
		}
	}
}

int main(int argc, char **argv)
{
	int i;
	is_tty = isatty(1);

	if (is_tty) {
		/* Open /dev/tty for interactive keystrokes */
		tty_in = open("/dev/tty", O_RDWR);
		if (tty_in < 0) {
			if (isatty(0))
				tty_in = 0;
		}
		get_window_size();
		enable_raw_mode();
	}

	if (argc <= 1) {
		/* Read from stdin */
		view_stream(stdin, NULL, 0, 0);
	} else {
		for (i = 1; i < argc; i++) {
			if (strcmp(argv[i], "-") == 0) {
				view_stream(stdin, "-", 0, 0);
				continue;
			}
			FILE *fp = fopen(argv[i], "r");
			if (!fp) {
				fprintf(stderr, "more: cannot open %s\n", argv[i]);
				continue;
			}
			struct stat st;
			off_t size = 0;
			int seekable = 0;
			if (fstat(fileno(fp), &st) == 0 && S_ISREG(st.st_mode)) {
				size = st.st_size;
				seekable = 1;
			}
			if (argc > 2 && is_tty) {
				printf("::::::::::::::\n%s\n::::::::::::::\n", argv[i]);
			}
			view_stream(fp, argv[i], size, seekable);
			fclose(fp);
		}
	}

	restore_terminal();
	return 0;
}
