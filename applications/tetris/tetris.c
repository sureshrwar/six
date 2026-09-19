/*
 * tetris.c - Colored ANSI terminal Tetris for SIX (/bin/tetris)
 *
 * Inspired by micro-tetris (Joachim Nilsson) and 4.3BSD tetris.
 * Uses raw termios (VMIN=0, VTIME=1 -> 100ms tick on HZ=10) and
 * VT100/ANSI color escape sequences over /dev/console.
 */

#include <stdio.h>
#include <stdlib.h>
#include <linux/string.h>
#include <linux/unistd.h>
#include <linux/termios.h>

#define BOARD_W 10
#define BOARD_H 20

/* Board cells store 0 (empty) or 1..7 (color index of locked piece) */
static unsigned char board[BOARD_H][BOARD_W];

/*
 * 7 standard tetrominoes, 4 rotations each, 4x4 bitmasks (16-bit int).
 * Colors:
 *   1 = Cyan    (I) - ANSI 46
 *   2 = Blue    (J) - ANSI 44
 *   3 = Yellow  (L) - ANSI 43
 *   4 = White   (O) - ANSI 47
 *   5 = Green   (S) - ANSI 42
 *   6 = Magenta (T) - ANSI 45
 *   7 = Red     (Z) - ANSI 41
 */
static const unsigned short shapes[7][4] = {
	/* I */
	{ 0x0F00, 0x2222, 0x00F0, 0x4444 },
	/* J */
	{ 0x44C0, 0x8E00, 0x6440, 0x0E20 },
	/* L */
	{ 0x4460, 0x0E80, 0xC440, 0x2E00 },
	/* O */
	{ 0x0660, 0x0660, 0x0660, 0x0660 },
	/* S */
	{ 0x06C0, 0x8C40, 0x6C00, 0x4620 },
	/* T */
	{ 0x0E40, 0x4C40, 0x4E00, 0x4640 },
	/* Z */
	{ 0x0C60, 0x4C80, 0xC600, 0x2640 }
};

static const int bg_colors[8] = {
	0, 46, 44, 43, 47, 42, 45, 41
};

static struct termios orig_tio;
static int tty_saved = 0;

static int cur_type, cur_rot, cur_x, cur_y;
static int next_type;
static int score = 0;
static int lines_cleared = 0;
static int level = 1;
static int game_over = 0;
static int paused = 0;
static unsigned long rng_state = 1;

static int next_rand(void)
{
	rng_state = rng_state * 1103515245UL + 12345UL;
	return (int)((rng_state >> 16) & 0x7fff);
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
	/* Hide cursor and clear screen */
	printf("\033[?25l\033[2J\033[H");
}

static void tty_restore(void)
{
	/* Reset attributes, show cursor, move below board */
	printf("\033[0m\033[?25h\033[%d;1H\n", BOARD_H + 3);
	if (tty_saved)
		tcsetattr(0, &orig_tio);
}

static int cell_at(int type, int rot, int r, int c)
{
	return (shapes[type][rot] & (0x8000 >> (r * 4 + c))) != 0;
}

static int fits(int type, int rot, int nx, int ny)
{
	int r, c;

	for (r = 0; r < 4; r++) {
		for (c = 0; c < 4; c++) {
			if (!cell_at(type, rot, r, c))
				continue;
			if (nx + c < 0 || nx + c >= BOARD_W || ny + r >= BOARD_H)
				return 0;
			if (ny + r >= 0 && board[ny + r][nx + c])
				return 0;
		}
	}
	return 1;
}

static int ghost_y(void)
{
	int gy = cur_y;

	while (fits(cur_type, cur_rot, cur_x, gy + 1))
		gy++;
	return gy;
}

static void spawn_piece(void)
{
	cur_type = next_type;
	next_type = next_rand() % 7;
	cur_rot = 0;
	cur_x = (BOARD_W / 2) - 2;
	cur_y = 0;

	if (!fits(cur_type, cur_rot, cur_x, cur_y))
		game_over = 1;
}

static void lock_piece(void)
{
	int r, c, row, full, cleared = 0;

	for (r = 0; r < 4; r++) {
		for (c = 0; c < 4; c++) {
			if (cell_at(cur_type, cur_rot, r, c) && cur_y + r >= 0)
				board[cur_y + r][cur_x + c] = (unsigned char)(cur_type + 1);
		}
	}

	/* Check for completed lines */
	for (row = BOARD_H - 1; row >= 0; row--) {
		full = 1;
		for (c = 0; c < BOARD_W; c++) {
			if (!board[row][c]) {
				full = 0;
				break;
			}
		}
		if (full) {
			int k;
			for (k = row; k > 0; k--)
				memcpy(board[k], board[k - 1], BOARD_W);
			memset(board[0], 0, BOARD_W);
			cleared++;
			row++; /* re-check shifted row */
		}
	}

	if (cleared > 0) {
		static const int line_pts[5] = { 0, 100, 300, 500, 800 };
		lines_cleared += cleared;
		score += line_pts[cleared] * level;
		level = 1 + (lines_cleared / 10);
		if (level > 9)
			level = 9;
	}

	spawn_piece();
}

static void draw_screen(void)
{
	int r, c;
	int gy = ghost_y();

	/* Move cursor home */
	printf("\033[H");
	printf("  \033[1;36m+--------------------+\033[0m   \033[1;33mSIX TETRIS\033[0m\n");

	for (r = 0; r < BOARD_H; r++) {
		printf("  \033[1;36m|\033[0m");
		for (c = 0; c < BOARD_W; c++) {
			int color = board[r][c];
			int is_ghost = 0;

			if (!color && !game_over) {
				if (r >= cur_y && r < cur_y + 4 &&
				    c >= cur_x && c < cur_x + 4 &&
				    cell_at(cur_type, cur_rot, r - cur_y, c - cur_x)) {
					color = cur_type + 1;
				} else if (r >= gy && r < gy + 4 &&
					   c >= cur_x && c < cur_x + 4 &&
					   cell_at(cur_type, cur_rot, r - gy, c - cur_x)) {
					is_ghost = 1;
				}
			}

			if (color) {
				printf("\033[%dm[]\033[0m", bg_colors[color]);
			} else if (is_ghost) {
				printf("\033[2m::\033[0m");
			} else {
				printf(" .");
			}
		}
		printf("\033[1;36m|\033[0m");

		/* Right-hand HUD */
		if (r == 1) {
			printf("   Score : \033[1m%d\033[0m   ", score);
		} else if (r == 2) {
			printf("   Lines : \033[1m%d\033[0m   ", lines_cleared);
		} else if (r == 3) {
			printf("   Level : \033[1m%d\033[0m   ", level);
		} else if (r == 5) {
			printf("   Next:");
		} else if (r >= 6 && r <= 9) {
			int nr = r - 6;
			printf("     ");
			for (c = 0; c < 4; c++) {
				if (cell_at(next_type, 0, nr, c))
					printf("\033[%dm[]\033[0m", bg_colors[next_type + 1]);
				else
					printf("  ");
			}
		} else if (r == 12) {
			printf("   \033[1mControls:\033[0m");
		} else if (r == 13) {
			printf("   Left/Right : a/d or Arrows");
		} else if (r == 14) {
			printf("   Rotate     : w   or Up");
		} else if (r == 15) {
			printf("   Soft Drop  : s   or Down");
		} else if (r == 16) {
			printf("   Hard Drop  : Space");
		} else if (r == 17) {
			printf("   Pause/Quit : p / q");
		} else if (r == 19 && paused) {
			printf("   \033[1;33m*** PAUSED ***\033[0m");
		} else if (r == 19) {
			printf("                 ");
		}
		printf("\n");
	}

	printf("  \033[1;36m+--------------------+\033[0m\n");
}

int main(int argc, char **argv)
{
	int tick = 0;
	int drop_interval;
	unsigned char ch;

	rng_state = (unsigned long)time(0) ^ (unsigned long)getpid();
	next_type = next_rand() % 7;
	spawn_piece();

	tty_raw();
	draw_screen();

	while (!game_over) {
		int n = read(0, (char *)&ch, 1);

		if (n == 1) {
			if (ch == 'q' || ch == 'Q' || ch == 3)
				break;
			if (ch == 'p' || ch == 'P') {
				paused = !paused;
				draw_screen();
				continue;
			}
			if (paused)
				continue;

			/* Arrow keys: ESC [ A/B/C/D */
			if (ch == 27) {
				unsigned char seq[2];
				if (read(0, (char *)&seq[0], 1) == 1 &&
				    seq[0] == '[' &&
				    read(0, (char *)&seq[1], 1) == 1) {
					if (seq[1] == 'A') ch = 'w'; /* Up: rotate */
					else if (seq[1] == 'B') ch = 's'; /* Down: drop */
					else if (seq[1] == 'C') ch = 'd'; /* Right */
					else if (seq[1] == 'D') ch = 'a'; /* Left */
				}
			}

			if ((ch == 'a' || ch == 'h') && fits(cur_type, cur_rot, cur_x - 1, cur_y)) {
				cur_x--;
			} else if ((ch == 'd' || ch == 'l') && fits(cur_type, cur_rot, cur_x + 1, cur_y)) {
				cur_x++;
			} else if ((ch == 'w' || ch == 'k') && fits(cur_type, (cur_rot + 1) & 3, cur_x, cur_y)) {
				cur_rot = (cur_rot + 1) & 3;
			} else if (ch == 's' || ch == 'j') {
				if (fits(cur_type, cur_rot, cur_x, cur_y + 1)) {
					cur_y++;
					score += 1;
				} else {
					lock_piece();
				}
			} else if (ch == ' ') {
				while (fits(cur_type, cur_rot, cur_x, cur_y + 1)) {
					cur_y++;
					score += 2;
				}
				lock_piece();
			}
			draw_screen();
		} else {
			/* 100ms timer tick expired */
			if (paused)
				continue;
			tick++;
			drop_interval = 6 - (level / 2);
			if (drop_interval < 1)
				drop_interval = 1;
			if (tick >= drop_interval) {
				tick = 0;
				if (fits(cur_type, cur_rot, cur_x, cur_y + 1))
					cur_y++;
				else
					lock_piece();
				draw_screen();
			}
		}
	}

	draw_screen();
	tty_restore();
	if (game_over)
		printf("Game Over! Final Score: %d (%d lines, Level %d)\n",
		       score, lines_cleared, level);
	return 0;
}
