/*
 * starwars.c - Standalone ASCII Star Wars (Episode IV) Player for SIX
 *
 * Plays Simon Jansen's famous ASCII Star Wars animation (ASCIIMATION).
 * Reads frames from /usr/lib/starwars.txt (or a file specified on the CLI).
 * If no local animation data is found, falls back to streaming from
 * towel.blinkenlights.nl via /bin/telnet.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <linux/termios.h>
#include <linux/time.h>
#include <linux/types.h>
#include <errno.h>

extern int close(int fd);
extern int read(int fd, void *buf, size_t count);
extern int write(int fd, const void *buf, size_t count);
extern int tcgetattr(int fd, struct termios *termios_p);
extern int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);
extern int select(int n, fd_set *inp, fd_set *outp, fd_set *exp, struct timeval *tvp);
extern int execve(const char *filename, char *const argv[], char *const envp[]);

#define FRAME_LINES  13
#define FRAME_COLS   67
#define TOTAL_FRAMES 3411
#define DEFAULT_FILE "/usr/lib/starwars.txt"
#define ALT_FILE     "applications/starwars/starwars.txt"

static struct termios orig_tio;
static int tty_saved = 0;

static void tty_restore(void)
{
	if (tty_saved) {
		printf("\033[?25h\033[0m\033[24;1H\n");
		fflush(stdout);
		tcsetattr(0, TCSANOW, &orig_tio);
		tty_saved = 0;
	}
}

static void sig_handler(int sig)
{
	(void)sig;
	tty_restore();
	exit(0);
}

static void tty_raw(void)
{
	struct termios t;
	if (tcgetattr(0, &orig_tio) == 0) {
		tty_saved = 1;
		t = orig_tio;
		t.c_lflag &= ~(ICANON | ECHO | ISIG);
		t.c_iflag &= ~(ICRNL | IXON);
		t.c_cc[VMIN] = 0;
		t.c_cc[VTIME] = 0;
		tcsetattr(0, TCSANOW, &t);
	}
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);
	signal(SIGHUP, sig_handler);

	/* Clear screen and hide cursor */
	printf("\033[?25l\033[2J\033[H");
	fflush(stdout);
}

/* Sleep for ms milliseconds while listening for keypresses */
/* Returns key character if pressed, 0 otherwise */
static int sleep_and_poll(int ms)
{
	struct timeval tv;
	fd_set rfds;

	while (ms > 0) {
		int chunk = (ms > 40) ? 40 : ms;
		tv.tv_sec = 0;
		tv.tv_usec = chunk * 1000;

		FD_ZERO(&rfds);
		FD_SET(0, &rfds);

		int s = select(1, &rfds, NULL, NULL, &tv);
		if (s > 0 && FD_ISSET(0, &rfds)) {
			char ch = 0;
			if (read(0, &ch, 1) > 0)
				return (unsigned char)ch;
		}
		ms -= chunk;
	}
	return 0;
}

static void play_animation(FILE *f)
{
	char line[256];
	char frame[FRAME_LINES][FRAME_COLS + 32];
	int frame_num = 0;
	int paused = 0;

	tty_raw();

	while (!feof(f)) {
		/* Line 1: delay count */
		if (!fgets(line, sizeof(line), f))
			break;

		/* Strip leading whitespace */
		char *p = line;
		while (*p == ' ' || *p == '\t' || *p == '\r') p++;
		if (*p == '\n' || *p == '\0')
			continue;

		int delay_units = atoi(p);
		if (delay_units <= 0) delay_units = 1;

		/* Next 13 lines: ASCII art frame */
		int i;
		for (i = 0; i < FRAME_LINES; i++) {
			if (fgets(frame[i], sizeof(frame[i]), f)) {
				char *nl = strchr(frame[i], '\n');
				if (nl) *nl = '\0';
				nl = strchr(frame[i], '\r');
				if (nl) *nl = '\0';
			} else {
				frame[i][0] = '\0';
			}
		}
		frame_num++;

		int pct = (frame_num * 100) / TOTAL_FRAMES;
		if (pct > 100) pct = 100;

		/* Header */
		printf("\033[H\033[1;33m=== STAR WARS: Episode IV (ASCII) ===\033[0m"
		       "  [Frame %d/%d (%d%%)] %s\033[K\r\n"
		       "\033[0;37mControls: [Space] Pause  [q] Quit  [f] +10s  [b] -10s  [r] Restart\033[0m\033[K\r\n"
		       "\033[K\r\n",
		       frame_num, TOTAL_FRAMES, pct,
		       paused ? "\033[1;31m[PAUSED]\033[0m" : "");

		/* 13 lines of ASCII art */
		for (i = 0; i < FRAME_LINES; i++) {
			printf("  %s\033[K\r\n", frame[i]);
		}
		/* Clear line below */
		printf("\033[K\r\n");
		fflush(stdout);

		/* Delay: each unit is 1/15th of a second (~67 ms) */
		int delay_ms = delay_units * 67;

		while (paused) {
			int ch = sleep_and_poll(80);
			if (ch == 'q' || ch == 'Q' || ch == 27) {
				tty_restore();
				return;
			}
			if (ch == ' ' || ch == '\r' || ch == '\n') {
				paused = 0;
				break;
			}
		}

		int key = sleep_and_poll(delay_ms);
		if (key == 'q' || key == 'Q' || key == 27) {
			break;
		} else if (key == ' ') {
			paused = 1;
		} else if (key == 'f' || key == 'F') {
			/* Skip forward 150 frames (~10 seconds) */
			int skip = 150 * 14;
			while (skip-- > 0 && fgets(line, sizeof(line), f)) ;
			frame_num += 150;
		} else if (key == 'b' || key == 'B') {
			/* Rewind ~150 frames */
			long cur = ftell(f);
			long back = 150 * 14 * 60;
			if (cur > back) {
				fseek(f, cur - back, SEEK_SET);
				while (fgets(line, sizeof(line), f)) {
					if (isdigit((unsigned char)line[0])) break;
				}
				frame_num -= 150;
				if (frame_num < 1) frame_num = 1;
			} else {
				rewind(f);
				frame_num = 0;
			}
		} else if (key == 'r' || key == 'R') {
			rewind(f);
			frame_num = 0;
		}
	}

	tty_restore();
	printf("\n\033[1;32m=== May the Force be with you! ===\033[0m\n\n");
}

static void fallback_telnet(void)
{
	printf("starwars: local starwars.txt not found.\n");
	printf("Connecting live to towel.blinkenlights.nl...\n");
	char *args[3];
	args[0] = "/bin/telnet";
	args[1] = "towel.blinkenlights.nl";
	args[2] = NULL;
	execve("/bin/telnet", args, NULL);
	perror("execve /bin/telnet");
	exit(1);
}

int main(int argc, char **argv)
{
	const char *filepath = NULL;
	FILE *f = NULL;

	if (argc > 1 && strcmp(argv[1], "-h") == 0) {
		printf("Usage: starwars [filename.txt]\n");
		printf("Plays the classic ASCII Star Wars Episode IV animation.\n");
		printf("Controls:\n");
		printf("  Space       Pause / Resume\n");
		printf("  q or Esc    Quit to shell\n");
		printf("  f           Fast-forward (+10s)\n");
		printf("  b           Rewind (-10s)\n");
		printf("  r           Restart from beginning\n");
		return 0;
	}

	if (argc > 1 && argv[1][0] != '-') {
		filepath = argv[1];
		f = fopen(filepath, "r");
	}

	if (!f) {
		filepath = DEFAULT_FILE;
		f = fopen(filepath, "r");
	}
	if (!f) {
		filepath = ALT_FILE;
		f = fopen(filepath, "r");
	}
	if (!f) {
		filepath = "starwars.txt";
		f = fopen(filepath, "r");
	}

	if (f) {
		play_animation(f);
		fclose(f);
		return 0;
	}

	fallback_telnet();
	return 0;
}
