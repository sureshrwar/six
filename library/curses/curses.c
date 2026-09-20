#include <curses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <stdarg.h>

static WINDOW static_stdscr;
WINDOW *stdscr = &static_stdscr;
WINDOW *curscr = &static_stdscr;

int LINES = 24;
int COLS = 80;

static struct termios orig_termios;
static int term_inited = 0;

WINDOW *initscr(void)
{
	struct winsize ws;

	if (ioctl(0, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
		LINES = ws.ws_row;
		COLS = ws.ws_col;
	} else {
		char *p;
		p = getenv("LINES");
		if (p && atoi(p) > 0)
			LINES = atoi(p);
		p = getenv("COLUMNS");
		if (p && atoi(p) > 0)
			COLS = atoi(p);
	}

	stdscr->_cury = 0;
	stdscr->_curx = 0;
	stdscr->_maxy = LINES;
	stdscr->_maxx = COLS;
	stdscr->_begy = 0;
	stdscr->_begx = 0;
	stdscr->_flags = 0;
	curscr = stdscr;

	if (!term_inited) {
		tcgetattr(0, &orig_termios);
		term_inited = 1;
	}

	return stdscr;
}

int endwin(void)
{
	printf("\033[0m\033[%d;1H\n", LINES);
	fflush(stdout);
	if (term_inited)
		tcsetattr(0, TCSANOW, &orig_termios);
	return OK;
}

int cbreak(void)
{
	struct termios t;
	if (tcgetattr(0, &t) == 0) {
		t.c_lflag &= ~ICANON;
		t.c_cc[VMIN] = 1;
		t.c_cc[VTIME] = 0;
		tcsetattr(0, TCSANOW, &t);
	}
	return OK;
}

int nocbreak(void)
{
	struct termios t;
	if (tcgetattr(0, &t) == 0) {
		t.c_lflag |= ICANON;
		tcsetattr(0, TCSANOW, &t);
	}
	return OK;
}

int crmode(void)
{
	return cbreak();
}

int nocrmode(void)
{
	return nocbreak();
}

int raw(void)
{
	struct termios t;
	if (tcgetattr(0, &t) == 0) {
		t.c_lflag &= ~(ICANON | ISIG | ECHO);
		t.c_cc[VMIN] = 1;
		t.c_cc[VTIME] = 0;
		tcsetattr(0, TCSANOW, &t);
	}
	return OK;
}

int noraw(void)
{
	struct termios t;
	if (tcgetattr(0, &t) == 0) {
		t.c_lflag |= (ICANON | ISIG);
		tcsetattr(0, TCSANOW, &t);
	}
	return OK;
}

int echo(void)
{
	struct termios t;
	if (tcgetattr(0, &t) == 0) {
		t.c_lflag |= (ECHO | ECHOE | ECHOK);
		tcsetattr(0, TCSANOW, &t);
	}
	return OK;
}

int noecho(void)
{
	struct termios t;
	if (tcgetattr(0, &t) == 0) {
		t.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);
		tcsetattr(0, TCSANOW, &t);
	}
	return OK;
}

int nl(void)
{
	return OK;
}

int nonl(void)
{
	return OK;
}

int keypad(WINDOW *win, int bf)
{
	return OK;
}

int nodelay(WINDOW *win, int bf)
{
	return OK;
}

int scrollok(WINDOW *win, int bf)
{
	return OK;
}

int move(int y, int x)
{
	if (y < 0) y = 0;
	if (y >= LINES) y = LINES - 1;
	if (x < 0) x = 0;
	if (x >= COLS) x = COLS - 1;
	stdscr->_cury = y;
	stdscr->_curx = x;
	printf("\033[%d;%dH", y + 1, x + 1);
	return OK;
}

int wmove(WINDOW *win, int y, int x)
{
	if (!win) return ERR;
	if (y < 0) y = 0;
	if (y >= win->_maxy) y = win->_maxy - 1;
	if (x < 0) x = 0;
	if (x >= win->_maxx) x = win->_maxx - 1;
	win->_cury = y;
	win->_curx = x;
	printf("\033[%d;%dH", win->_begy + y + 1, win->_begx + x + 1);
	return OK;
}

int addch(char ch)
{
	if (ch == '\n') {
		stdscr->_curx = 0;
		if (stdscr->_cury + 1 < LINES)
			stdscr->_cury++;
		putchar('\n');
	} else if (ch == '\r') {
		stdscr->_curx = 0;
		putchar('\r');
	} else if (ch == '\b') {
		if (stdscr->_curx > 0) {
			stdscr->_curx--;
			putchar('\b');
		}
	} else {
		putchar(ch);
		stdscr->_curx++;
		if (stdscr->_curx >= COLS) {
			stdscr->_curx = 0;
			if (stdscr->_cury + 1 < LINES)
				stdscr->_cury++;
		}
	}
	return OK;
}

int waddch(WINDOW *win, char ch)
{
	if (!win) return ERR;
	printf("\033[%d;%dH", win->_begy + win->_cury + 1, win->_begx + win->_curx + 1);
	putchar(ch);
	win->_curx++;
	if (win->_curx >= win->_maxx) {
		win->_curx = 0;
		if (win->_cury + 1 < win->_maxy)
			win->_cury++;
	}
	return OK;
}

int addstr(const char *str)
{
	if (!str) return ERR;
	while (*str)
		addch(*str++);
	return OK;
}

int waddstr(WINDOW *win, const char *str)
{
	if (!win || !str) return ERR;
	while (*str)
		waddch(win, *str++);
	return OK;
}

int mvaddch(int y, int x, char ch)
{
	move(y, x);
	return addch(ch);
}

int mvwaddch(WINDOW *win, int y, int x, char ch)
{
	wmove(win, y, x);
	return waddch(win, ch);
}

int mvaddstr(int y, int x, const char *str)
{
	move(y, x);
	return addstr(str);
}

int mvwaddstr(WINDOW *win, int y, int x, const char *str)
{
	wmove(win, y, x);
	return waddstr(win, str);
}

int printw(const char *fmt, ...)
{
	char buf[2048];
	va_list ap;
	va_start(ap, fmt);
	vsprintf(buf, fmt, ap);
	va_end(ap);
	return addstr(buf);
}

int wprintw(WINDOW *win, const char *fmt, ...)
{
	char buf[2048];
	va_list ap;
	va_start(ap, fmt);
	vsprintf(buf, fmt, ap);
	va_end(ap);
	return waddstr(win, buf);
}

int refresh(void)
{
	fflush(stdout);
	return OK;
}

int wrefresh(WINDOW *win)
{
	fflush(stdout);
	return OK;
}

int clear(void)
{
	printf("\033[2J\033[H");
	if (stdscr) {
		stdscr->_cury = 0;
		stdscr->_curx = 0;
	}
	return OK;
}

int wclear(WINDOW *win)
{
	int y;
	if (!win) return ERR;
	for (y = 0; y < win->_maxy; y++) {
		wmove(win, y, 0);
		wclrtoeol(win);
	}
	wmove(win, 0, 0);
	return OK;
}

int clrtoeol(void)
{
	printf("\033[K");
	return OK;
}

int wclrtoeol(WINDOW *win)
{
	int i;
	if (!win) return ERR;
	printf("\033[%d;%dH", win->_begy + win->_cury + 1, win->_begx + win->_curx + 1);
	for (i = win->_curx; i < win->_maxx; i++)
		putchar(' ');
	printf("\033[%d;%dH", win->_begy + win->_cury + 1, win->_begx + win->_curx + 1);
	return OK;
}

int clrtobot(void)
{
	printf("\033[J");
	return OK;
}

int wclrtobot(WINDOW *win)
{
	int y;
	if (!win) return ERR;
	wclrtoeol(win);
	for (y = win->_cury + 1; y < win->_maxy; y++) {
		wmove(win, y, 0);
		wclrtoeol(win);
	}
	return OK;
}

int standout(void)
{
	printf("\033[7m");
	return OK;
}

int standend(void)
{
	printf("\033[0m");
	return OK;
}

int wstandout(WINDOW *win)
{
	return standout();
}

int wstandend(WINDOW *win)
{
	return standend();
}

int attrset(int attr)
{
	printf("\033[0m");
	if (attr & (A_STANDOUT | A_REVERSE))
		printf("\033[7m");
	if (attr & A_BOLD)
		printf("\033[1m");
	if (attr & A_UNDERLINE)
		printf("\033[4m");
	return OK;
}

int attroff(int attr)
{
	printf("\033[0m");
	return OK;
}

int wattrset(WINDOW *win, int attr)
{
	return attrset(attr);
}

int wattroff(WINDOW *win, int attr)
{
	return attroff(attr);
}

int getch(void)
{
	unsigned char ch;
	fflush(stdout);
	if (read(0, &ch, 1) <= 0)
		return ERR;
	return (int)ch;
}

int wgetch(WINDOW *win)
{
	return getch();
}

WINDOW *newwin(int nlines, int ncols, int begin_y, int begin_x)
{
	WINDOW *w = (WINDOW *)malloc(sizeof(WINDOW));
	if (!w) return NULL;
	w->_maxy = nlines ? nlines : (LINES - begin_y);
	w->_maxx = ncols ? ncols : (COLS - begin_x);
	w->_begy = begin_y;
	w->_begx = begin_x;
	w->_cury = 0;
	w->_curx = 0;
	w->_flags = 0;
	return w;
}

int delwin(WINDOW *win)
{
	if (win && win != stdscr)
		free(win);
	return OK;
}

int touchwin(WINDOW *win)
{
	return OK;
}

int box(WINDOW *win, char vert, char horiz)
{
	int x, y;
	if (!win) return ERR;
	if (!vert) vert = '|';
	if (!horiz) horiz = '-';

	for (x = 0; x < win->_maxx; x++) {
		wmove(win, 0, x);
		putchar(horiz);
		wmove(win, win->_maxy - 1, x);
		putchar(horiz);
	}
	for (y = 0; y < win->_maxy; y++) {
		wmove(win, y, 0);
		putchar(vert);
		wmove(win, y, win->_maxx - 1);
		putchar(vert);
	}
	wmove(win, 0, 0); putchar('+');
	wmove(win, 0, win->_maxx - 1); putchar('+');
	wmove(win, win->_maxy - 1, 0); putchar('+');
	wmove(win, win->_maxy - 1, win->_maxx - 1); putchar('+');
	wmove(win, 1, 1);
	return OK;
}

int scroll(WINDOW *win)
{
	return OK;
}

int winsertln(WINDOW *win)
{
	return OK;
}

int insertln(void)
{
	return OK;
}

int deleteln(void)
{
	return OK;
}

int wdeleteln(WINDOW *win)
{
	return OK;
}
