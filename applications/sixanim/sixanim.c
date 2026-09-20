/*
 * sixanim.c - Animated in-place rotating boot logo for SIX
 *
 * Rotates the letters S I X through 3 dimensions strictly in-place using
 * ANSI cursor repositioning (\033[5A and \r\033[K).
 * Left-aligned by default to match standard UNIX boot logging.
 * Pre-allocates the vertical bounding box once so zero intermediate
 * frames pollute the terminal's scrollback buffer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/termios.h>

#define NUM_FRAMES 4
#define FRAME_ROWS 5
#define ART_WIDTH 20

static const char *frames[NUM_FRAMES][FRAME_ROWS] = {
	/* Frame 0: Front face */
	{
		" ____   ___  __  __ ",
		"/ ___| |_ _| \\ \\/ / ",
		"\\___ \\  | |   \\  /  ",
		" ___) | | |   /  \\  ",
		"|____/ |___| /_/\\_\\ "
	},
	/* Frame 1: 45° perspective turn */
	{
		"  /__/   / /   / /  ",
		" /__    / /    \\/   ",
		"   /   / /     /\\   ",
		"__/   /_/    _/  \\_ ",
		"                    "
	},
	/* Frame 2: Edge-on */
	{
		"   |      |     |   ",
		"   |      |     |   ",
		"   |      |     |   ",
		"   |      |     |   ",
		"   |      |     |   "
	},
	/* Frame 3: Other 45° perspective turn */
	{
		"  \\__\\   \\ \\   \\ \\  ",
		"  __\\     \\ \\   \\/  ",
		"  \\        \\ \\  /\\  ",
		"   \\__    \\_\\ \\_  \\ ",
		"                    "
	}
};

static int term_cols = 80;
static int cursor_hidden = 0;

static void cleanup(void)
{
	if (cursor_hidden) {
		printf("\033[0m\033[?25h");
		fflush(stdout);
		cursor_hidden = 0;
	}
}

static void sig_handler(int sig)
{
	cleanup();
	exit(0);
}

static void msleep(int ms)
{
	struct timespec ts;
	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (ms % 1000) * 1000000L;
	nanosleep(&ts, NULL);
}

static void get_term_size(void)
{
	struct winsize ws;
	if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
		term_cols = ws.ws_col;
	} else {
		char *cols = getenv("COLUMNS");
		if (cols) term_cols = atoi(cols);
	}
	if (term_cols < 30) term_cols = 30;
}

static void print_padding(int spaces)
{
	int i;
	for (i = 0; i < spaces; i++)
		putchar(' ');
}

int main(int argc, char **argv)
{
	int i, step;
	int is_tty = isatty(1);

	puts("");

	get_term_size();

	/* Left-aligned by default; support -c for centered if requested */
	int pad_x = 0;
	if (argc > 1 && strcmp(argv[1], "-c") == 0) {
		pad_x = (term_cols - ART_WIDTH) / 2;
		if (pad_x < 0) pad_x = 0;
	}

	if (!is_tty) {
		/* Non-interactive fallback: static front face */
		putchar('\n');
		for (i = 0; i < FRAME_ROWS; i++) {
			if (pad_x > 0) print_padding(pad_x);
			printf("%s\n", frames[0][i]);
		}
		if (pad_x > 0) print_padding(pad_x + 2);
		printf("[ R.I.P HELLRAISER ]\n\n");
		printf("[ SYSTEM READY ]\n\n");
		return 0;
	}

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	/*
	 * Reserve vertical space for the animation (5 rows + 2 status rows)
	 * upfront so the terminal never scrolls during the animation cycle.
	 */
	for (i = 0; i < FRAME_ROWS + 2; i++)
		putchar('\n');
	printf("\033[%dA", FRAME_ROWS + 2);
	fflush(stdout);

	/* Hide cursor during animation */
	printf("\033[?25l");
	cursor_hidden = 1;

	/* Spin 3 full revolutions (12 steps) strictly in place */
	for (step = 0; step < 12; step++) {
		int f = step % 4;

		if (step > 0) {
			/* Move cursor back up FRAME_ROWS lines to rewrite in-place */
			printf("\033[%dA", FRAME_ROWS);
		}

		/* Subtle color shifts as the glyphs rotate */
		if (f == 0) printf("\033[1;36m");       /* Bright cyan front */
		else if (f == 1) printf("\033[1;34m");  /* Blue angle */
		else if (f == 2) printf("\033[1;35m");  /* Magenta edge */
		else printf("\033[1;34m");              /* Blue other angle */

		for (i = 0; i < FRAME_ROWS; i++) {
			printf("\r\033[K");
			if (pad_x > 0) print_padding(pad_x);
			printf("%s\n", frames[f][i]);
		}
		printf("\033[0m");
		fflush(stdout);
		msleep(90);
	}

	/* Rest on the final front face (in-place) */
	printf("\033[%dA", FRAME_ROWS);
	printf("\033[1;36m");
	for (i = 0; i < FRAME_ROWS; i++) {
		printf("\r\033[K");
		if (pad_x > 0) print_padding(pad_x);
		printf("%s\n", frames[0][i]);
	}
	printf("\033[0m\n");

	printf("\r\033[K");
	if (pad_x > 0) print_padding(pad_x + 2);
	printf("\033[1;32m[ R.I.P. HELLRAISER ]\033[0m\n\n");
	printf("\033[1;32m[ SYSTEM READY ]\033[0m\n\n");
	fflush(stdout);

	cleanup();
	return 0;
}
