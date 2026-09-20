/*
 * sl.c - Steam Locomotive for SIX (/bin/sl)
 *
 * Inspired by Toyoda Masashi's sl (1992).
 * Runs an animated steam train across the terminal.
 */

#include <stdio.h>
#include <stdlib.h>
#include <linux/string.h>
#include <linux/unistd.h>
#include <linux/time.h>
#include <linux/signal.h>
#include <linux/termios.h>

#define TRAIN_HEIGHT 8
#define TRAIN_WIDTH  54
#define SCREEN_WIDTH 80
#define SCREEN_START_ROW 10

static const char *train_lines[2][TRAIN_HEIGHT] = {
	{
		"     (@@)  (  )  (@)  (@@@)                       ",
		"   ====        ________                ___________",
		" _D _|  |_______/        \\__I_I_____===__|_________|",
		"  |(_)---  |   H\\________/ _____ \\   |  | ===|     |",
		" /     |  |   H  |  |     |   |     |  |    |     |",
		" |      |  |   H  |__--------------------| [___]   |",
		" | ________|___H__/__|_____/[][]~\\_______|       |  ",
		"   (O)(O)      (O)(O)(O)       (O)(O)     (O)(O)    "
	},
	{
		"      (@@)  (@@)  (  )  (@)                       ",
		"   ====        ________                ___________",
		" _D _|  |_______/        \\__I_I_____===__|_________|",
		"  |(_)---  |   H\\________/ _____ \\   |  | ===|     |",
		" /     |  |   H  |  |     |   |     |  |    |     |",
		" |      |  |   H  |__--------------------| [___]   |",
		" | ________|___H__/__|_____/[][]~\\_______|       |  ",
		"   (o)(o)      (o)(o)(o)       (o)(o)     (o)(o)    "
	}
};

static struct termios orig_tio;
static int tty_saved = 0;

static void msleep(int ms)
{
	struct timespec ts;
	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (ms % 1000) * 1000000L;
	nanosleep(&ts, NULL);
}

static void tty_raw(void)
{
	struct termios t;

	if (tcgetattr(0, &orig_tio) == 0) {
		tty_saved = 1;
		t = orig_tio;
		t.c_lflag &= ~(ICANON | ECHO);
		tcsetattr(0, &t);
	}
	/* Hide cursor */
	printf("\033[?25l");
	fflush(stdout);
}

static void tty_restore(void)
{
	/* Show cursor, reset attributes, move below */
	printf("\033[0m\033[?25h\033[%d;1H\n", SCREEN_START_ROW + TRAIN_HEIGHT + 2);
	fflush(stdout);
	if (tty_saved)
		tcsetattr(0, &orig_tio);
}

/* Draws a string starting at x, clipped to 1..SCREEN_WIDTH */
static void draw_clipped(int row, int x, const char *str)
{
	int len = strlen(str);
	int start_char = 0;
	int print_x = x;
	int print_len = len;

	if (x < 1) {
		start_char = 1 - x;
		print_x = 1;
		print_len = len - start_char;
	}
	if (print_x + print_len - 1 > SCREEN_WIDTH) {
		print_len = SCREEN_WIDTH - print_x + 1;
	}

	if (print_len > 0 && start_char < len) {
		printf("\033[%d;%dH\033[2K\033[%d;%dH%.*s",
		       row, 1, row, print_x, print_len, str + start_char);
	} else {
		printf("\033[%d;1H\033[2K", row);
	}
}

int main(int argc, char **argv)
{
	int x;
	int frame = 0;

	/* In true classic sl style, ignore SIGINT! */
	signal(SIGINT, SIG_IGN);

	tty_raw();

	/* Clear train area */
	for (x = 0; x < TRAIN_HEIGHT; x++)
		printf("\033[%d;1H\033[2K", SCREEN_START_ROW + x);
	fflush(stdout);

	/* Slide train from x = 80 down to -TRAIN_WIDTH */
	for (x = SCREEN_WIDTH; x >= -TRAIN_WIDTH; x -= 3) {
		int r;
		int f = (frame / 2) % 2;

		for (r = 0; r < TRAIN_HEIGHT; r++) {
			draw_clipped(SCREEN_START_ROW + r, x, train_lines[f][r]);
		}
		fflush(stdout);
		msleep(40);
		frame++;
	}

	/* Clear rows when finished */
	for (x = 0; x < TRAIN_HEIGHT; x++)
		printf("\033[%d;1H\033[2K", SCREEN_START_ROW + x);
	fflush(stdout);

	tty_restore();
	return 0;
}
