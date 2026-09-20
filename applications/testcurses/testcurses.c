#include <curses.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
	WINDOW *w;
	int y, x;

	printf("--- Testing curses library ---\n");
	initscr();

	if (!stdscr) {
		printf("FAIL: stdscr is NULL\n");
		return 1;
	}

	if (LINES <= 0 || COLS <= 0) {
		printf("FAIL: invalid LINES (%d) or COLS (%d)\n", LINES, COLS);
		endwin();
		return 1;
	}

	clear();
	move(2, 5);
	addstr("Hello from curses on SIX!");

	getyx(stdscr, y, x);
	if (y != 2 || x != 5 + strlen("Hello from curses on SIX!")) {
		endwin();
		printf("FAIL: getyx mismatch: expected (2, %d), got (%d, %d)\n",
		       5 + (int)strlen("Hello from curses on SIX!"), y, x);
		return 1;
	}

	move(4, 5);
	standout();
	addstr("[HIGHLIGHTED TEXT]");
	standend();

	move(6, 5);
	printw("Formatted printw: LINES=%d COLS=%d", LINES, COLS);

	/* Test window / box */
	w = newwin(6, 40, 8, 10);
	if (!w) {
		endwin();
		printf("FAIL: newwin returned NULL\n");
		return 1;
	}
	box(w, '|', '-');
	wmove(w, 2, 4);
	waddstr(w, "Inside Window Box!");
	wrefresh(w);
	delwin(w);

	move(16, 5);
	addstr("Curses tests completed successfully.");
	refresh();

	endwin();

	printf("SUCCESS: All curses functions executed and verified!\n");
	return 0;
}
