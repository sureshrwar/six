/*
 * sixanim.c - Animated 3D rotating boot logo for SIX
 *
 * Rotates the letters S I X through 3 dimensions and brings
 * them to rest with the glowing SIX emblem and [ SYSTEM READY ].
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

static int term_rows = 24;
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
	if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) {
		term_rows = ws.ws_row;
		term_cols = ws.ws_col;
	} else {
		char *lines = getenv("LINES");
		char *cols = getenv("COLUMNS");
		if (lines) term_rows = atoi(lines);
		if (cols) term_cols = atoi(cols);
	}
	if (term_rows < 8) term_rows = 8;
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

	if (!is_tty) {
		/* Non-interactive: just print front face */
		for (i = 0; i < FRAME_ROWS; i++) {
			printf("%s\n", frames[0][i]);
		}
		printf("\n   [ SYSTEM READY ]\n");
		return 0;
	}

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	get_term_size();

	int pad_x = (term_cols - ART_WIDTH) / 2;
	int pad_y = (term_rows - (FRAME_ROWS + 3)) / 2;
	if (pad_x < 0) pad_x = 0;
	if (pad_y < 0) pad_y = 0;

	/* Hide cursor */
	printf("\033[?25l");
	cursor_hidden = 1;

	/* Spin 3 full revolutions (12 steps) */
	for (step = 0; step < 12; step++) {
		int f = step % 4;
		printf("\033[H\033[2J"); /* Clear screen */
		for (i = 0; i < pad_y; i++)
			putchar('\n');

		/* Subtle color shift as it turns */
		if (f == 0) printf("\033[1;36m");       /* Bright cyan front */
		else if (f == 1) printf("\033[1;34m");  /* Blue angle */
		else if (f == 2) printf("\033[1;35m");  /* Magenta edge */
		else printf("\033[1;34m");              /* Blue other angle */

		for (i = 0; i < FRAME_ROWS; i++) {
			print_padding(pad_x);
			printf("%s\n", frames[f][i]);
		}
		printf("\033[0m");
		fflush(stdout);
		msleep(90);
	}

	/* Final resting face */
	printf("\033[H\033[2J");
	for (i = 0; i < pad_y; i++)
		putchar('\n');

	printf("\033[1;36m");
	for (i = 0; i < FRAME_ROWS; i++) {
		print_padding(pad_x);
		printf("%s\n", frames[0][i]);
	}
	printf("\033[0m\n\n");

	print_padding(pad_x + 2);
	printf("\033[1;32m[ SYSTEM READY ]\033[0m\n\n");
	fflush(stdout);

	cleanup();
	return 0;
}
