#ifndef _LIFE_CURSES_H
#define _LIFE_CURSES_H

#include <stdio.h>
#include <stdlib.h>
#include <linux/types.h>
#include <linux/time.h>
#include <linux/signal.h>
#include <linux/fcntl.h>
#include <linux/ioctl.h>
#include <linux/termios.h>

#define FALSE 0
#define TRUE  1
#define _PROTOTYPE(fn, args) fn args

typedef struct {
	int y0, x0;
	int rows, cols;
	int cury, curx;
} WINDOW;

static WINDOW _win_pool[4];
static int _win_cnt = 0;
static struct termios _life_orig_tio;
static int _life_tio_saved = 0;
static void initscr(void)
{
	struct termios t;
	if (tcgetattr(0, &_life_orig_tio) == 0) {
		_life_tio_saved = 1;
		t = _life_orig_tio;
		t.c_lflag &= ~(ICANON | ECHO);
		t.c_cc[VMIN] = 1;
		t.c_cc[VTIME] = 0;
		ioctl(0, TCSETS, &t);
	}
	write(1, "\033[H\033[2J", 7);
}

static void endwin(void)
{
	write(1, "\033[?25h\033[24;1H\n", 14);
	if (_life_tio_saved) {
		ioctl(0, TCSETS, &_life_orig_tio);
		_life_tio_saved = 0;
	}
}

static void noecho(void) {}

static void curs_set(int v)
{
	if (v == 0)
		write(1, "\033[?25l", 6);
	else
		write(1, "\033[?25h", 6);
}

static WINDOW *newwin(int rows, int cols, int y0, int x0)
{
	WINDOW *w = &_win_pool[_win_cnt & 3];
	_win_cnt++;
	w->y0 = y0;
	w->x0 = x0;
	w->rows = rows;
	w->cols = cols;
	w->cury = 0;
	w->curx = 0;
	return w;
}

static void scrollok(WINDOW *w, int flag) {}

static void wmove(WINDOW *w, int y, int x)
{
	char buf[24];
	if (!w) return;
	w->cury = y;
	w->curx = x;
	sprintf(buf, "\033[%d;%dH", w->y0 + y + 1, w->x0 + x + 1);
	write(1, buf, strlen(buf));
}

static void move(int y, int x)
{
	char buf[24];
	sprintf(buf, "\033[%d;%dH", y + 1, x + 1);
	write(1, buf, strlen(buf));
}

static void wclear(WINDOW *w)
{
	if (!w || w->rows > 1) {
		write(1, "\033[H\033[2J", 7);
	} else {
		wmove(w, 0, 0);
		write(1, "\033[K", 3);
	}
}

static void wclrtoeol(WINDOW *w)
{
	write(1, "\033[K", 3);
}

static void mvwaddch(WINDOW *w, int y, int x, char c)
{
	wmove(w, y, x);
	write(1, &c, 1);
}

#define wprintw(w, fmt, ...) do { \
	char _pbuf[160]; \
	sprintf(_pbuf, fmt, __VA_ARGS__); \
	write(1, _pbuf, strlen(_pbuf)); \
} while (0)

void cleanup(int s);

static void refresh(void) {}

static int _life_nonblock = 0;

static void wrefresh(WINDOW *w)
{
	volatile int d;
	struct termios nb;
	unsigned char ch = 0;

	if (!w || w->rows <= 1)
		return;

	/* Small pacing delay so generations are visible on modern CPUs */
	for (d = 0; d < 4000000; d++)
		;

	if (!_life_nonblock && _life_tio_saved) {
		nb = _life_orig_tio;
		nb.c_lflag &= ~(ICANON | ECHO);
		nb.c_cc[VMIN] = 0;
		nb.c_cc[VTIME] = 0;
		ioctl(0, TCSETS, &nb);
		_life_nonblock = 1;
	}
	if (read(0, &ch, 1) == 1 && (ch == 'q' || ch == 'Q' || ch == 0x03))
		cleanup(0);
}

static int wgetch(WINDOW *w)
{
	unsigned char c = 0;
	if (read(0, &c, 1) <= 0)
		return 0;
	return (int)c;
}

#endif
