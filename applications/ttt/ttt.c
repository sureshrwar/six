/* tic tac toe (noughts and crosses)		Author: Warren Toomey */

/* Copyright 1988 by Warren Toomey	wkt@cs.adfa.oz.au[@uunet.uu.net]
 *
 * You may freely copy or distribute this code as long as this notice
 * remains intact.
 *
 * You may modify this code, as long as this notice remains intact, and
 * you add another notice indicating that the code has been modified.
 *
 * You may NOT sell this code or in any way profit from this code without
 * prior agreement from the author.
 */

/* Compile with cc -o tic tic.c -lcurses -ltermcap */

#include <stdlib.h>
#include <linux/time.h>
#include <stdio.h>

#define printw printf
#define _PROTOTYPE(fn, args) fn args

static int abs(int x)
{
  return x < 0 ? -x : x;
}

static int ttt_readline(char *buf, int maxlen)
{
  int pos = 0;
  char c;
  fflush(stdout);
  while (read(0, &c, 1) == 1) {
	if (c == '\r' || c == '\n') {
		buf[pos] = '\0';
		return pos;
	}
	if (pos < maxlen - 1)
		buf[pos++] = c;
  }
  buf[pos] = '\0';
  return pos > 0 ? pos : -1;
}

typedef struct {
  int value;			/* The move returned by the    */
  int path;			/* alphabeta consists of a value */
} MOVE;				/* and an actual move (path)   */

_PROTOTYPE(int main, (void));
_PROTOTYPE(int stateval, (int board [], int whosemove));
_PROTOTYPE(MOVE alphabeta, (int board [], int whosemove, int alpha, int beta));
_PROTOTYPE(void draw, (int board []));
_PROTOTYPE(void getmove, (int board []));
_PROTOTYPE(int endofgame, (int board []));
_PROTOTYPE(int randommove, (void));

 /* Static evaluator. Returns 100 if we have 3 in a row -100 if they have 3
  * in a row
  * 
  * Board is array of 9 ints, where 0=empty square 1=our move 4= their move
  * 
  * and board is indices	0 1 2 3 4 5 6 7 8 */


int stateval(board, whosemove)
int board[];
int whosemove;
{
  static int row[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8},	/* Indices of 3in-a-rows */
			{0, 3, 6}, {1, 4, 7}, {2, 5, 8},
			{0, 4, 8}, {2, 4, 6}};

  int temp;			/* Temp row results */
  int i, j;			/* Loop counters */
  int side;			/* Depth multiplier */
  int win, lose;

  if (whosemove == 1) {
	win = 100;
	lose = -100;
	side = 1;
  } else {
	/* Multiply by -1 if */
	win = -100;
	lose = 100;
	side = -1;
  }				/* not out move */
  for (i = 0; i < 8; i++) {	/* For every 3-in-a-row */
	temp = 0;
	for (j = 0; j < 3; j++)	/* Add up the board values */
		temp += board[row[i][j]];

	if (temp == 3) return(win);	/* We've got 3 in a row */
	if (temp == 12) return (lose);	/* They've got 3 in a row */
  }
  return(0);			/* Finally return sum */
}


MOVE alphabeta(board, whosemove, alpha, beta)	/* Alphabeta: takes a board, */
int board[];			/* whose move, alpha & beta cutoffs, */
int whosemove;			/* and returns a move to make and */
int alpha;			/* the value that the move has */
int beta;
{
  MOVE result, successor;
  int best_score, i, best_path, mademove;

  result.value = stateval(board, whosemove);	/* Work out the board's */
  /* Static value */
  if ((result.value == 100) ||	/* If a win or loss already */
      (result.value == -100))
	return(result);	/* return the result */

  best_score = beta;		/* Ok, set worst score */
  mademove = 0;			/* to the beta cutoff */
  for (i = 0; i < 9; i++) {
	if (board[i] == 0) {	/* For all valid moves */
		mademove = 1;
		board[i] = whosemove;	/* make the move on board */
		successor = alphabeta(board, 5 - whosemove, -best_score - 1, -alpha - 1);
		/* Get value of the move */
		board[i] = 0;	/* Take move back */
		if (-successor.value > best_score) {	/* If a better score */
			best_score = -successor.value;	/* update our score */
			best_path = i;	/* and move */
			if (best_score > alpha)
				break;	/* If we've beaten alpha */
		}		/* return immediately */
	}
  }
  if (mademove) {
	result.value = best_score;	/* Finally return best score */
	result.path = best_path;/* and best move */
  }
  return(result);		/* If no move, return static result */
}


void draw(board)			/* Draw the board */
int board[];
{
  int i, j, row;
  static char out[] = " X  O";	/* Lookup table for character */

  row = 6;
#ifdef CURSES
  move(row, 0);
#endif
  for (j = 0; j < 9; j += 3) {
	printw(" %d | %d | %d     ", j, j + 1, j + 2);
	for (i = 0; i < 3; i++) {
		printw("%c ", out[board[j + i]]);
		if (i < 2) printw("| ");
	}
	if (j < 4) {
#ifdef CURSES
		move(++row, 0);
#else
		printw("\n");
#endif
		printw("---+---+---   ---+---+---");
	}
#ifdef CURSES
	move(++row, 0);
#else
	printw("\n");
#endif
  }
#ifdef CURSES
  refresh();
#else
  printw("\n");
#endif
}


void getmove(board)			/* Get a player's move */
int board[];
{
  int Move;
  char line[32];

  do {
	for (;;) {
		printw("Your move (0-8, or q to quit): ");
		if (ttt_readline(line, sizeof(line)) < 0 || line[0] == 'q' || line[0] == 'Q')
			exit(0);
		if (line[0] >= '0' && line[0] <= '8' && line[1] == '\0') {
			Move = line[0] - '0';
			break;
		}
	}
  }
  while (Move < 0 || Move > 8 || board[Move]);
  board[Move] = 4;		/* If legal, add to board */
  draw(board);			/* Draw the board */
}


int endofgame(board)		/* Determine end of the game */
int board[];
{
  int eval;
  int count;

  eval = stateval(board, 1);
#ifdef CURSES
  move(20, 25);
#endif
  if (eval == 100) {
	printw("I have beaten you.\n");
	return(1);
  }
  if (eval == -100) {
	printw("Bus error (core dumped)\n");
	return(1);
  }
  count = 0;
  for (eval = 0; eval < 9; eval++)
	if (board[eval] != 0) count++;
  if (count == 9) {
	printw("A draw!\n");
	return(1);
  }
#ifdef CURSES
  refresh();
#endif
  return(0);
}


int randommove()
{				/* Make an initial random move */
  int i;

  i = abs((int) time((long *) 0));
  return(i % 9);
}


int main()
{				/* The actual game */
  int i, board[9];
  char line[32];
  char ch;
  MOVE ourmove;

  for (i = 0; i < 9; i++) board[i] = 0;	/* Initialise the board */
  printw("                           TIC TAC TOE   \n\n");
  printw("                        Your moves are 'O'\n");
  printw("                         My moves are 'X'\n\n");
  do {
	printw("Do you wish to move first (y/n): ");
	if (ttt_readline(line, sizeof(line)) < 0)
		return(0);
	ch = line[0];
  } while (ch == '\0');
  if ((ch != 'y') && (ch != 'Y')) {
	i = randommove();	/* If we move first */
	board[i] = 1;		/* make it random */
#ifdef CURSES
	move(7, 42);
	printw("My move: %d\n", i);
	refresh();
#else
	printw("My move: %d\n", i);
#endif
  }
  draw(board);
  getmove(board);

  while (1) {
	ourmove = alphabeta(board, 1, 99, -99);	/* Get a move for us;
						 * return wins */
	/* Immediately & ignore losses */
	board[ourmove.path] = 1;/* and make it */
#ifdef CURSES
	move(7, 42);
	printw("My move: %d\n", ourmove.path);
	refresh();
#else
	printw("My move: %d\n", ourmove.path);
#endif
	draw(board);
	if (endofgame(board)) break;	/* If end of game, exit */
	getmove(board);		/* Get opponent's move */
	if (endofgame(board)) break;	/* If end of game, exit */
  }
#ifdef CURSES
  endwin();
#endif
  return(0);
}
