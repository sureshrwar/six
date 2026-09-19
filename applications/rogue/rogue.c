/*
 * rogue.c - Classic Terminal Roguelike for SIX (/bin/rogue)
 *
 * Inspired by 1980 BSD Rogue (Toy, Wichman, Arnold).
 * Pure ANSI/VT100 terminal turn-based dungeon crawler.
 */

#include <stdio.h>
#include <stdlib.h>
#include <linux/string.h>
#include <linux/unistd.h>
#include <linux/termios.h>

#ifndef abs
#define abs(x) ((x) < 0 ? -(x) : (x))
#endif

#define MAP_W 78
#define MAP_H 21

#define T_EMPTY    ' '
#define T_FLOOR    '.'
#define T_HWALL    '-'
#define T_VWALL    '|'
#define T_DOOR     '+'
#define T_CORR     '#'
#define T_STAIRS   '>'
#define T_GOLD     '$'
#define T_POTION   '!'
#define T_WEAPON   ')'
#define T_ARMOR    '['

struct room {
	int x, y, w, h;
	int cx, cy;
};

struct item {
	int x, y;
	char type;
	int val;
	int active;
};

struct monster {
	int x, y;
	char ch;
	const char *name;
	int hp, max_hp;
	int atk;
	int exp;
	int active;
};

#define MAX_ITEMS 16
#define MAX_MONS  12

static char map[MAP_H][MAP_W];
static unsigned char seen[MAP_H][MAP_W];
static struct item items[MAX_ITEMS];
static struct monster mons[MAX_MONS];
static struct room rooms[3][3];

/* Player stats */
static int px, py;
static int hp = 20, max_hp = 20;
static int gold = 0;
static int level = 1;
static int depth = 1;
static int exp_cur = 0;
static int exp_next = 15;
static int potions = 2;
static int weapon_bonus = 1;
static const char *weapon_name = "Mace";
static int armor_bonus = 1;
static const char *armor_name = "Leather";

static char msg_buf[128];
static struct termios orig_tio;
static int tty_saved = 0;
static unsigned long rng_state = 1;

static int next_rand(void)
{
	rng_state = rng_state * 1103515245UL + 12345UL;
	return (int)((rng_state >> 16) & 0x7fff);
}

static int rand_range(int low, int high)
{
	if (high <= low) return low;
	return low + (next_rand() % (high - low + 1));
}

static void tty_raw(void)
{
	struct termios t;

	if (tcgetattr(0, &orig_tio) == 0) {
		tty_saved = 1;
		t = orig_tio;
		t.c_lflag &= ~(ICANON | ECHO | ISIG);
		t.c_iflag &= ~(ICRNL | IXON);
		t.c_cc[VMIN] = 1;
		t.c_cc[VTIME] = 0;
		tcsetattr(0, &t);
	}
	printf("\033[?25l\033[2J\033[H");
	fflush(stdout);
}

static void tty_restore(void)
{
	printf("\033[0m\033[?25h\033[24;1H\n");
	fflush(stdout);
	if (tty_saved)
		tcsetattr(0, &orig_tio);
}

static int is_walkable(int x, int y)
{
	char c;
	int i;

	if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H)
		return 0;
	c = map[y][x];
	if (c != T_FLOOR && c != T_CORR && c != T_DOOR && c != T_STAIRS)
		return 0;
	for (i = 0; i < MAX_MONS; i++) {
		if (mons[i].active && mons[i].x == x && mons[i].y == y)
			return 0;
	}
	return 1;
}

static void reveal_around(int cx, int cy)
{
	int dy, dx, rx, ry;

	/* If inside a room, reveal the entire room */
	for (ry = 0; ry < 3; ry++) {
		for (rx = 0; rx < 3; rx++) {
			struct room *r = &rooms[ry][rx];
			if (cx >= r->x && cx < r->x + r->w &&
			    cy >= r->y && cy < r->y + r->h) {
				int y, x;
				for (y = r->y - 1; y <= r->y + r->h; y++) {
					for (x = r->x - 1; x <= r->x + r->w; x++) {
						if (x >= 0 && x < MAP_W && y >= 0 && y < MAP_H)
							seen[y][x] = 1;
					}
				}
				return;
			}
		}
	}

	/* In corridor: reveal 1-tile radius */
	for (dy = -1; dy <= 1; dy++) {
		for (dx = -1; dx <= 1; dx++) {
			int y = cy + dy;
			int x = cx + dx;
			if (x >= 0 && x < MAP_W && y >= 0 && y < MAP_H)
				seen[y][x] = 1;
		}
	}
}

static void dig_h_corridor(int x1, int x2, int y)
{
	int x, start = x1 < x2 ? x1 : x2;
	int end = x1 < x2 ? x2 : x1;
	for (x = start; x <= end; x++) {
		if (map[y][x] == T_EMPTY || map[y][x] == T_HWALL || map[y][x] == T_VWALL)
			map[y][x] = (map[y][x] == T_EMPTY) ? T_CORR : T_DOOR;
	}
}

static void dig_v_corridor(int y1, int y2, int x)
{
	int y, start = y1 < y2 ? y1 : y2;
	int end = y1 < y2 ? y2 : y1;
	for (y = start; y <= end; y++) {
		if (map[y][x] == T_EMPTY || map[y][x] == T_HWALL || map[y][x] == T_VWALL)
			map[y][x] = (map[y][x] == T_EMPTY) ? T_CORR : T_DOOR;
	}
}

static void spawn_monster(int x, int y)
{
	int i;
	for (i = 0; i < MAX_MONS; i++) {
		if (!mons[i].active) {
			int roll = rand_range(1, 100) + (depth * 10);
			mons[i].x = x;
			mons[i].y = y;
			mons[i].active = 1;
			if (roll < 45) {
				mons[i].ch = 'b';
				mons[i].name = "Bat";
				mons[i].hp = mons[i].max_hp = 5 + depth;
				mons[i].atk = 2 + depth;
				mons[i].exp = 5;
			} else if (roll < 75) {
				mons[i].ch = 'g';
				mons[i].name = "Goblin";
				mons[i].hp = mons[i].max_hp = 9 + (depth * 2);
				mons[i].atk = 4 + depth;
				mons[i].exp = 10;
			} else if (roll < 95) {
				mons[i].ch = 'o';
				mons[i].name = "Orc";
				mons[i].hp = mons[i].max_hp = 15 + (depth * 3);
				mons[i].atk = 6 + (depth * 2);
				mons[i].exp = 20;
			} else if (roll < 120) {
				mons[i].ch = 't';
				mons[i].name = "Troll";
				mons[i].hp = mons[i].max_hp = 25 + (depth * 4);
				mons[i].atk = 9 + (depth * 2);
				mons[i].exp = 40;
			} else {
				mons[i].ch = 'D';
				mons[i].name = "Dragon";
				mons[i].hp = mons[i].max_hp = 45 + (depth * 5);
				mons[i].atk = 14 + (depth * 3);
				mons[i].exp = 100;
			}
			break;
		}
	}
}

static void spawn_item(int x, int y)
{
	int i;
	for (i = 0; i < MAX_ITEMS; i++) {
		if (!items[i].active) {
			int roll = rand_range(1, 100);
			items[i].x = x;
			items[i].y = y;
			items[i].active = 1;
			if (roll < 45) {
				items[i].type = T_GOLD;
				items[i].val = rand_range(10, 40) * depth;
			} else if (roll < 75) {
				items[i].type = T_POTION;
				items[i].val = 1;
			} else if (roll < 90) {
				items[i].type = T_WEAPON;
				items[i].val = 1 + (depth / 2);
			} else {
				items[i].type = T_ARMOR;
				items[i].val = 1 + (depth / 2);
			}
			break;
		}
	}
}

static void generate_level(void)
{
	int ry, rx, y, x, i;
	int start_rx, start_ry, stairs_rx, stairs_ry;

	memset(map, T_EMPTY, sizeof(map));
	memset(seen, 0, sizeof(seen));
	for (i = 0; i < MAX_MONS; i++) mons[i].active = 0;
	for (i = 0; i < MAX_ITEMS; i++) items[i].active = 0;

	/* Create 3x3 rooms */
	for (ry = 0; ry < 3; ry++) {
		for (rx = 0; rx < 3; rx++) {
			int cell_x = rx * 26 + 1;
			int cell_y = ry * 7 + 1;
			int w = rand_range(6, 18);
			int h = rand_range(3, 5);
			int ox = cell_x + rand_range(0, 24 - w);
			int oy = cell_y + rand_range(0, 5 - h);

			rooms[ry][rx].x = ox;
			rooms[ry][rx].y = oy;
			rooms[ry][rx].w = w;
			rooms[ry][rx].h = h;
			rooms[ry][rx].cx = ox + (w / 2);
			rooms[ry][rx].cy = oy + (h / 2);

			/* Walls and floor */
			for (y = oy; y < oy + h; y++) {
				for (x = ox; x < ox + w; x++)
					map[y][x] = T_FLOOR;
			}
			for (x = ox - 1; x <= ox + w; x++) {
				map[oy - 1][x] = T_HWALL;
				map[oy + h][x] = T_HWALL;
			}
			for (y = oy; y < oy + h; y++) {
				map[y][ox - 1] = T_VWALL;
				map[y][ox + w] = T_VWALL;
			}

			/* Place loot / monster */
			if (rand_range(1, 10) <= 7)
				spawn_item(rooms[ry][rx].cx, rooms[ry][rx].cy);
			if (rand_range(1, 10) <= 6)
				spawn_monster(rooms[ry][rx].cx + 1, rooms[ry][rx].cy);
		}
	}

	/* Connect horizontal corridors */
	for (ry = 0; ry < 3; ry++) {
		dig_h_corridor(rooms[ry][0].cx, rooms[ry][1].cx, rooms[ry][0].cy);
		dig_h_corridor(rooms[ry][1].cx, rooms[ry][2].cx, rooms[ry][1].cy);
	}
	/* Connect vertical corridors */
	for (rx = 0; rx < 3; rx++) {
		dig_v_corridor(rooms[0][rx].cy, rooms[1][rx].cy, rooms[0][rx].cx);
		dig_v_corridor(rooms[1][rx].cy, rooms[2][rx].cy, rooms[1][rx].cx);
	}

	/* Place player in one room, stairs in a different room */
	start_rx = rand_range(0, 2);
	start_ry = rand_range(0, 2);
	px = rooms[start_ry][start_rx].cx;
	py = rooms[start_ry][start_rx].cy;

	do {
		stairs_rx = rand_range(0, 2);
		stairs_ry = rand_range(0, 2);
	} while (stairs_rx == start_rx && stairs_ry == start_ry);
	map[rooms[stairs_ry][stairs_rx].cy][rooms[stairs_rx][stairs_rx].cx] = T_STAIRS;

	reveal_around(px, py);
	sprintf(msg_buf, "Welcome to dungeon level %d. Find the stairs (>)!", depth);
}

static void draw_screen(void)
{
	int y, x, i;

	/* Top message line */
	printf("\033[H\033[2K\033[1;33m%s\033[0m\n", msg_buf);

	/* Dungeon map rows 1..21 */
	for (y = 0; y < MAP_H; y++) {
		for (x = 0; x < MAP_W; x++) {
			char ch = map[y][x];
			int color = 0;

			if (x == px && y == py) {
				printf("\033[1;37m@\033[0m");
				continue;
			}

			/* Monsters on visible tiles */
			for (i = 0; i < MAX_MONS; i++) {
				if (mons[i].active && mons[i].x == x && mons[i].y == y && seen[y][x]) {
					ch = mons[i].ch;
					color = 31; /* Bright Red */
					break;
				}
			}

			/* Items on visible tiles */
			if (!color) {
				for (i = 0; i < MAX_ITEMS; i++) {
					if (items[i].active && items[i].x == x && items[i].y == y && seen[y][x]) {
						ch = items[i].type;
						if (ch == T_GOLD) color = 33;     /* Yellow */
						else if (ch == T_POTION) color = 32;/* Green */
						else color = 36;                  /* Cyan */
						break;
					}
				}
			}

			if (!seen[y][x]) {
				putchar(' ');
			} else if (color) {
				printf("\033[1;%dm%c\033[0m", color, ch);
			} else if (ch == T_FLOOR) {
				printf("\033[2m.\033[0m");
			} else if (ch == T_STAIRS) {
				printf("\033[1;35m>\033[0m");
			} else if (ch == T_DOOR) {
				printf("\033[1;33m+\033[0m");
			} else {
				putchar(ch);
			}
		}
		putchar('\n');
	}

	/* Status line */
	printf("\033[2K\033[1mLevel: %d  Gold: %d  Hp: %d(%d)  Str: %d  Arm: %d  Exp: %d/%d  Potions: %d\033[0m",
	       depth, gold, hp, max_hp, 14 + weapon_bonus, 2 + armor_bonus,
	       exp_cur, exp_next, potions);
	fflush(stdout);
}

static void player_attack(struct monster *m)
{
	int dmg = rand_range(2, 6) + weapon_bonus + (level * 2);
	m->hp -= dmg;
	sprintf(msg_buf, "You hit the %s for %d damage!", m->name, dmg);
	if (m->hp <= 0) {
		m->active = 0;
		exp_cur += m->exp;
		sprintf(msg_buf, "You defeated the %s! (+%d exp)", m->name, m->exp);
		if (exp_cur >= exp_next) {
			level++;
			exp_next = exp_next * 2;
			max_hp += 6;
			hp = max_hp;
			sprintf(msg_buf, "Welcome to level %d! Max HP increased.", level);
		}
	}
}

static void monster_turn(void)
{
	int i;
	for (i = 0; i < MAX_MONS; i++) {
		struct monster *m = &mons[i];
		int dx, dy, dist;

		if (!m->active) continue;

		dx = px - m->x;
		dy = py - m->y;
		dist = abs(dx) + abs(dy);

		/* Attack if adjacent */
		if (dist == 1) {
			int dmg = rand_range(1, m->atk) - armor_bonus;
			if (dmg < 1) dmg = 1;
			hp -= dmg;
			sprintf(msg_buf, "The %s hits you for %d damage!", m->name, dmg);
			if (hp <= 0) {
				hp = 0;
				draw_screen();
				printf("\n\n\033[1;31m*** You died on dungeon level %d! Final gold: %d ***\033[0m\n",
				       depth, gold);
				tty_restore();
				exit(0);
			}
		} else if (dist <= 8 && seen[m->y][m->x]) {
			/* Walk toward player if seen */
			int nx = m->x + (dx > 0 ? 1 : (dx < 0 ? -1 : 0));
			int ny = m->y + (dy > 0 ? 1 : (dy < 0 ? -1 : 0));
			if (is_walkable(nx, ny)) {
				m->x = nx;
				m->y = ny;
			}
		}
	}
}

static void handle_step(int dx, int dy)
{
	int nx = px + dx;
	int ny = py + dy;
	int i;

	if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H)
		return;

	/* Check for monster collision -> attack */
	for (i = 0; i < MAX_MONS; i++) {
		if (mons[i].active && mons[i].x == nx && mons[i].y == ny) {
			player_attack(&mons[i]);
			monster_turn();
			draw_screen();
			return;
		}
	}

	/* Check wall collision */
	if (map[ny][nx] == T_HWALL || map[ny][nx] == T_VWALL || map[ny][nx] == T_EMPTY) {
		sprintf(msg_buf, "Ouch! You bump into a wall.");
		draw_screen();
		return;
	}

	/* Move player */
	px = nx;
	py = ny;
	reveal_around(px, py);
	msg_buf[0] = '\0';

	/* Pick up items */
	for (i = 0; i < MAX_ITEMS; i++) {
		if (items[i].active && items[i].x == px && items[i].y == py) {
			items[i].active = 0;
			if (items[i].type == T_GOLD) {
				gold += items[i].val;
				sprintf(msg_buf, "You found %d gold pieces!", items[i].val);
			} else if (items[i].type == T_POTION) {
				potions++;
				sprintf(msg_buf, "You found a potion of healing! (Press 'd' to drink)");
			} else if (items[i].type == T_WEAPON) {
				weapon_bonus += items[i].val;
				weapon_name = (weapon_bonus > 3) ? "Longsword +2" : "Broadsword";
				sprintf(msg_buf, "You equipped a %s! (Atk increased)", weapon_name);
			} else if (items[i].type == T_ARMOR) {
				armor_bonus += items[i].val;
				armor_name = (armor_bonus > 3) ? "Plate Armor" : "Chainmail";
				sprintf(msg_buf, "You equipped %s! (Def increased)", armor_name);
			}
			break;
		}
	}

	monster_turn();
	draw_screen();
}

int main(int argc, char **argv)
{
	unsigned char ch;

	rng_state = (unsigned long)time(0) ^ (unsigned long)getpid();
	generate_level();
	tty_raw();
	draw_screen();

	while (hp > 0) {
		if (read(0, (char *)&ch, 1) != 1)
			continue;

		if (ch == 'q' || ch == 'Q') {
			sprintf(msg_buf, "Really quit? (y/n)");
			draw_screen();
			if (read(0, (char *)&ch, 1) == 1 && (ch == 'y' || ch == 'Y'))
				break;
			msg_buf[0] = '\0';
			draw_screen();
			continue;
		}

		/* Arrow keys: ESC [ A/B/C/D */
		if (ch == 27) {
			unsigned char seq[2];
			if (read(0, (char *)&seq[0], 1) == 1 && seq[0] == '[' &&
			    read(0, (char *)&seq[1], 1) == 1) {
				if (seq[1] == 'A') handle_step(0, -1);
				else if (seq[1] == 'B') handle_step(0, 1);
				else if (seq[1] == 'C') handle_step(1, 0);
				else if (seq[1] == 'D') handle_step(-1, 0);
			}
			continue;
		}

		/* Movement: vi keys (h/j/k/l) and diagonals (y/u/b/n) */
		if (ch == 'h') handle_step(-1, 0);
		else if (ch == 'l') handle_step(1, 0);
		else if (ch == 'j') handle_step(0, 1);
		else if (ch == 'k') handle_step(0, -1);
		else if (ch == 'y') handle_step(-1, -1);
		else if (ch == 'u') handle_step(1, -1);
		else if (ch == 'b') handle_step(-1, 1);
		else if (ch == 'n') handle_step(1, 1);
		else if (ch == 'd') {
			/* Drink potion */
			if (potions > 0) {
				potions--;
				hp += 12;
				if (hp > max_hp) hp = max_hp;
				sprintf(msg_buf, "You drink a healing potion (+12 HP)!");
				monster_turn();
			} else {
				sprintf(msg_buf, "You have no healing potions left!");
			}
			draw_screen();
		} else if (ch == '>') {
			/* Stairs */
			if (map[py][px] == T_STAIRS) {
				depth++;
				generate_level();
				draw_screen();
			} else {
				sprintf(msg_buf, "There are no stairs here to descend!");
				draw_screen();
			}
		} else if (ch == '.' || ch == ' ') {
			/* Wait a turn */
			sprintf(msg_buf, "You wait a turn.");
			monster_turn();
			draw_screen();
		} else if (ch == '?') {
			sprintf(msg_buf, "Keys: Arrows/hjkl/wasd: move/atk | d: drink potion | >: stairs | q: quit");
			draw_screen();
		}
	}

	tty_restore();
	printf("Thanks for playing SIX Rogue! Final depth: %d, Gold: %d\n", depth, gold);
	return 0;
}
