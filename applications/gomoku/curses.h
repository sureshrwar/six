#ifndef _GOMOKU_CURSES_H
#define _GOMOKU_CURSES_H

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <linux/types.h>
#include <linux/time.h>
#include <linux/ioctl.h>
#include <linux/termios.h>

#define FALSE 0
#define TRUE  1
#define LINES 24
#define COLS  80
#define _PROTOTYPE(fn, args) fn args

#define ACS_ULCORNER '+'
#define ACS_URCORNER '+'
#define ACS_LLCORNER '+'
#define ACS_LRCORNER '+'
#define ACS_HLINE    '-'
#define ACS_VLINE    '|'
#define ACS_PLUS     '+'
#define ACS_TTEE     '+'
#define ACS_BTEE     '+'
#define ACS_LTEE     '+'
#define ACS_RTEE     '+'
#define ACS_UARROW   '^'
#define ACS_DARROW   'v'
#define ACS_LARROW   '<'
#define ACS_RARROW   '>'
#define A_BLINK      1

static struct termios _gm_orig_tio;
static int _gm_tio_saved = 0;
static unsigned long _gm_rand_next = 1;

static void srand(unsigned int seed)
{
	time_t now = 0;
	time(&now);
	_gm_rand_next = (seed ^ (unsigned int)now) ? (seed ^ (unsigned int)now) : 1;
}

static int rand(void)
{
	_gm_rand_next = _gm_rand_next * 1103515245UL + 12345UL;
	return (int)((_gm_rand_next >> 16) & 0x7fff);
}

static void initscr(void)
{
	struct termios t;
	if (tcgetattr(0, &_gm_orig_tio) == 0) {
		_gm_tio_saved = 1;
		t = _gm_orig_tio;
		t.c_lflag &= ~(ICANON | ECHO);
		t.c_oflag |= (OPOST | ONLCR);
		t.c_cc[VMIN] = 1;
		t.c_cc[VTIME] = 0;
		ioctl(0, TCSETS, &t);
	}
	write(1, "\033[H\033[2J", 7);
}

static void raw(void) {}
static void noecho(void) {}

static void clear(void)
{
	write(1, "\033[H\033[2J", 7);
}

static void endwin(void)
{
	write(1, "\033[0m\033[24;1H\n", 12);
	if (_gm_tio_saved) {
		ioctl(0, TCSETS, &_gm_orig_tio);
		_gm_tio_saved = 0;
	}
}

static void move(int y, int x)
{
	char buf[24];
	sprintf(buf, "\033[%d;%dH", y + 1, x + 1);
	write(1, buf, strlen(buf));
}

static void addch(int c)
{
	char ch = (char)c;
	write(1, &ch, 1);
}

static void addstr(char *s)
{
	if (s)
		write(1, s, strlen(s));
}

static void mvaddch(int y, int x, int c)
{
	move(y, x);
	addch(c);
}

static void mvaddstr(int y, int x, char *s)
{
	move(y, x);
	addstr(s);
}

#define printw(...) do { \
	char _pbuf[160]; \
	sprintf(_pbuf, __VA_ARGS__); \
	write(1, _pbuf, strlen(_pbuf)); \
} while (0)

#define mvprintw(y, x, ...) do { \
	move(y, x); \
	printw(__VA_ARGS__); \
} while (0)

static void clrtoeol(void)
{
	write(1, "\033[K", 3);
}

static void standout(void)
{
	write(1, "\033[7m", 4);
}

static void standend(void)
{
	write(1, "\033[0m", 4);
}

static void attron(int a)
{
	write(1, "\033[5m", 4);
}

static void attroff(int a)
{
	write(1, "\033[0m", 4);
}

static void refresh(void) {}

static int getch(void)
{
	unsigned char c = 0;
	if (read(0, &c, 1) <= 0)
		return -1;
	return (int)c;
}

void abort(void)
{
	endwin();
	exit(1);
}

#endif
