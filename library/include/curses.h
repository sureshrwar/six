#ifndef _CURSES_H
#define _CURSES_H

#include <stdio.h>

#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

#ifndef OK
#define OK    0
#define ERR   (-1)
#endif

#ifndef bool
typedef int bool;
#endif

typedef struct _win_st {
	short _cury;
	short _curx;
	short _maxy;
	short _maxx;
	short _begy;
	short _begx;
	short _flags;
} WINDOW;

extern WINDOW *stdscr;
extern WINDOW *curscr;
extern int LINES;
extern int COLS;

/* Coordinates & queries */
#define getyx(win, y, x)   do { (y) = (win)->_cury; (x) = (win)->_curx; } while (0)
#define getcurx(win)       ((win)->_curx)
#define getcury(win)       ((win)->_cury)
#define getmaxy(win)       ((win)->_maxy)
#define getmaxx(win)       ((win)->_maxx)
#define getbegy(win)       ((win)->_begy)
#define getbegx(win)       ((win)->_begx)

/* Attributes */
#define A_NORMAL      0
#define A_STANDOUT    0x0001
#define A_UNDERLINE   0x0002
#define A_REVERSE     0x0004
#define A_BOLD        0x0008

/* Key codes */
#define KEY_DOWN      0402
#define KEY_UP        0403
#define KEY_LEFT      0404
#define KEY_RIGHT     0405
#define KEY_HOME      0406
#define KEY_BACKSPACE 0407
#define KEY_NPAGE     0522
#define KEY_PPAGE     0523
#define KEY_CLEAR     0515
#define KEY_LL        0533
#define KEY_A1        0534
#define KEY_A3        0535
#define KEY_B2        0536
#define KEY_C1        0537
#define KEY_C3        0540
#define KEY_END       0550
#define KEY_HELP      0553

/* Initialization and control */
WINDOW *initscr(void);
int endwin(void);
int cbreak(void);
int nocbreak(void);
int crmode(void);
int nocrmode(void);
int raw(void);
int noraw(void);
int echo(void);
int noecho(void);
int nl(void);
int nonl(void);
int keypad(WINDOW *win, int bf);
int nodelay(WINDOW *win, int bf);
int scrollok(WINDOW *win, int bf);

/* Cursor and character positioning */
int move(int y, int x);
int wmove(WINDOW *win, int y, int x);
int addch(char ch);
int waddch(WINDOW *win, char ch);
int mvaddch(int y, int x, char ch);
int mvwaddch(WINDOW *win, int y, int x, char ch);
int addstr(const char *str);
int waddstr(WINDOW *win, const char *str);
int mvaddstr(int y, int x, const char *str);
int mvwaddstr(WINDOW *win, int y, int x, const char *str);
int printw(const char *fmt, ...);
int wprintw(WINDOW *win, const char *fmt, ...);

/* Screen clearing and update */
int refresh(void);
int wrefresh(WINDOW *win);
int clear(void);
int wclear(WINDOW *win);
int erase(void);
int clrtoeol(void);
int wclrtoeol(WINDOW *win);
int clrtobot(void);
int wclrtobot(WINDOW *win);

/* Character inspection and window options */
int inch(void);
int winch(WINDOW *win);
int mvinch(int y, int x);
int mvwinch(WINDOW *win, int y, int x);
int leaveok(WINDOW *win, bool bf);

/* Attribute control */
int standout(void);
int standend(void);
int wstandout(WINDOW *win);
int wstandend(WINDOW *win);
int attrset(int attr);
int attroff(int attr);
int wattrset(WINDOW *win, int attr);
int wattroff(WINDOW *win, int attr);

/* Input */
int getch(void);
int wgetch(WINDOW *win);

/* Windows */
WINDOW *newwin(int nlines, int ncols, int begin_y, int begin_x);
int delwin(WINDOW *win);
int touchwin(WINDOW *win);
int box(WINDOW *win, char vert, char horiz);
int scroll(WINDOW *win);
int winsertln(WINDOW *win);
int insertln(void);
int deleteln(void);
int wdeleteln(WINDOW *win);

#endif
