/*
 * matrix.c - Falling Green Digital Rain for SIX (/bin/matrix)
 *
 * Inspired by Chris Allegretta's cmatrix (1999).
 * Uses raw termios (VMIN=0, VTIME=1 -> 100ms tick on HZ=10) and
 * ANSI escape codes over /dev/console.
 *
 * Exits cleanly on any keypress (or 'q').
 */

#include <stdio.h>
#include <stdlib.h>
#include <linux/string.h>
#include <linux/unistd.h>
#include <linux/termios.h>
#include <asm/ioctls.h>

#define MAX_COLS 132
#define MAX_ROWS 60

struct drop {
	int y;        /* current head row (< 0 means waiting to fall) */
	int len;      /* length of the drop trail */
	int speed;    /* ticks between downward steps (1..3) */
	int tick;     /* current tick accumulator */
};

static struct termios orig_tio;
static int tty_saved = 0;
static struct drop drops[MAX_COLS];
static int screen_cols = 80;
static int screen_rows = 24;
static unsigned long rng_state = 1;

static int next_rand(void)
{
	rng_state = rng_state * 1103515245UL + 12345UL;
	return (int)((rng_state >> 16) & 0x7fff);
}

/* Returns a random Matrix-style printable glyph */
static char random_glyph(void)
{
	static const char glyphs[] =
		"0123456789"
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"@#$%&*+-=~<>{}[]/?!|:;";
	int idx = next_rand() % (sizeof(glyphs) - 1);
	return glyphs[idx];
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
		t.c_cc[VTIME] = 1; /* 100 ms tick (1 jiffy at HZ=10) */
		tcsetattr(0, &t);
	}
	/* Hide cursor and clear screen with black background */
	printf("\033[?25l\033[40m\033[2J\033[H");
	fflush(stdout);
}

static void tty_restore(void)
{
	/* Reset attributes, show cursor, clear screen, move home */
	printf("\033[0m\033[?25h\033[2J\033[H");
	fflush(stdout);
	if (tty_saved)
		tcsetattr(0, &orig_tio);
}

static void init_drop(int x)
{
	drops[x].y = -(next_rand() % screen_rows);
	drops[x].len = 6 + (next_rand() % 14);
	drops[x].speed = 1 + (next_rand() % 2);
	drops[x].tick = 0;
}

int main(int argc, char **argv)
{
	struct winsize ws;
	int x;
	unsigned char ch;

	/* Seed pseudo-random generator */
	rng_state = (unsigned long)time(0) ^ (unsigned long)getpid();

	/* Detect screen dimensions */
	if (ioctl(0, TIOCGWINSZ, (char *)&ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
		screen_cols = ws.ws_col;
		screen_rows = ws.ws_row;
	}
	if (screen_cols > MAX_COLS) screen_cols = MAX_COLS;
	if (screen_rows > MAX_ROWS) screen_rows = MAX_ROWS;

	/* Initialize falling drops across all columns */
	for (x = 0; x < screen_cols; x++)
		init_drop(x);

	tty_raw();

	for (;;) {
		int n = read(0, (char *)&ch, 1);
		if (n > 0)
			break; /* Any keypress exits */

		/* Update drops and draw rain */
		for (x = 0; x < screen_cols; x++) {
			int head, tail, mid;

			drops[x].tick++;
			if (drops[x].tick < drops[x].speed)
				continue;
			drops[x].tick = 0;

			head = drops[x].y;
			tail = head - drops[x].len;
			mid = head - (drops[x].len / 2);

			/* Erase cell just behind the fading tail */
			if (tail >= 0 && tail < screen_rows) {
				printf("\033[%d;%dH ", tail + 1, x + 1);
			}

			/* Fading tail: dim green */
			if (tail + 1 >= 0 && tail + 1 < screen_rows) {
				printf("\033[%d;%dH\033[2;32m%c\033[0m",
				       tail + 2, x + 1, random_glyph());
			}

			/* Mid trail: normal green */
			if (mid >= 0 && mid < screen_rows) {
				printf("\033[%d;%dH\033[0;32m%c\033[0m",
				       mid + 1, x + 1, random_glyph());
			}

			/* Previous head: bright green */
			if (head - 1 >= 0 && head - 1 < screen_rows) {
				printf("\033[%d;%dH\033[1;32m%c\033[0m",
				       head, x + 1, random_glyph());
			}

			/* Leading head: bright bold white */
			if (head >= 0 && head < screen_rows) {
				printf("\033[%d;%dH\033[1;37m%c\033[0m",
				       head + 1, x + 1, random_glyph());
			}

			/* Occasional random glyph mutation in active stream */
			if (head > 2 && (next_rand() % 5) == 0) {
				int mut_y = head - 1 - (next_rand() % (drops[x].len > 4 ? drops[x].len - 2 : 2));
				if (mut_y >= 0 && mut_y < screen_rows) {
					printf("\033[%d;%dH\033[1;32m%c\033[0m",
					       mut_y + 1, x + 1, random_glyph());
				}
			}

			drops[x].y++;
			/* If the whole trail has fallen past the bottom, respawn */
			if (drops[x].y - drops[x].len >= screen_rows)
				init_drop(x);
		}

		fflush(stdout);
	}

	tty_restore();
	return 0;
}
