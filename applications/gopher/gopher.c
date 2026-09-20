/*
 * gopher.c -- Lightweight Interactive Terminal Gopher Browser (RFC 1436)
 *
 * Supports menu navigation, text pager, search queries, URL links, history
 * stack (back/forward), and saving documents to disk.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <termios.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>

#define GOPHER_PORT 70
#define DEFAULT_HOST "sdf.org"

/* Gopher Item Types (RFC 1436) */
#define TYPE_TEXT    '0'
#define TYPE_DIR     '1'
#define TYPE_CSO     '2'
#define TYPE_ERROR   '3'
#define TYPE_BINHEX  '4'
#define TYPE_DOS     '5'
#define TYPE_UUENCODE '6'
#define TYPE_SEARCH  '7'
#define TYPE_TELNET  '8'
#define TYPE_BIN     '9'
#define TYPE_GIF     'g'
#define TYPE_IMAGE   'I'
#define TYPE_HTML    'h'
#define TYPE_SOUND   's'
#define TYPE_INFO    'i'

struct GopherItem {
	char type;
	char *display;
	char *selector;
	char *host;
	int port;
	int is_navigable;
};

struct GopherMenu {
	char *host;
	int port;
	char *selector;
	char *title;
	struct GopherItem *items;
	int count;
	int capacity;
};

struct HistoryNode {
	char *host;
	int port;
	char *selector;
	char *title;
	int cursor;
	int top;
	struct HistoryNode *next;
};

static struct termios orig_termios;
static int raw_mode_active = 0;
static int term_rows = 24;
static int term_cols = 80;
static struct HistoryNode *history_stack = NULL;

/* -------------------------------------------------------------------------
 * Terminal & Screen Handling
 * ------------------------------------------------------------------------- */

static void update_winsize(void)
{
	struct winsize ws;
	if (ioctl(0, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
		term_rows = ws.ws_row;
		term_cols = ws.ws_col;
	} else {
		term_rows = 24;
		term_cols = 80;
	}
}

static void term_restore(void)
{
	if (raw_mode_active) {
		/* Show cursor, exit alternate screen, reset scroll region */
		write(1, "\033[?25h\033[r\033[?1049l", 17);
		tcsetattr(0, TCSAFLUSH, &orig_termios);
		raw_mode_active = 0;
	}
}

static void sigint_handler(int sig)
{
	(void)sig;
	term_restore();
	_exit(0);
}

static void term_raw(void)
{
	struct termios raw;
	if (raw_mode_active)
		return;

	tcgetattr(0, &orig_termios);
	raw = orig_termios;
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	tcsetattr(0, TCSAFLUSH, &raw);
	raw_mode_active = 1;

	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigint_handler);

	/* Enter alternate screen, hide cursor, clear */
	write(1, "\033[?1049h\033[?25l\033[2J\033[H", 18);
	update_winsize();
}

static int read_key(void)
{
	char c;
	int n = read(0, &c, 1);
	if (n <= 0)
		return -1;

	if (c == 27) { /* ESC sequence */
		char seq[5];
		if (read(0, &seq[0], 1) <= 0) return 27;
		if (read(0, &seq[1], 1) <= 0) return 27;

		if (seq[0] == '[') {
			if (seq[1] >= '0' && seq[1] <= '9') {
				char seq2;
				if (read(0, &seq2, 1) <= 0) return 27;
				if (seq2 == '~') {
					switch (seq[1]) {
					case '1': return 1001; /* Home */
					case '4': return 1002; /* End */
					case '5': return 1003; /* PageUp */
					case '6': return 1004; /* PageDown */
					case '7': return 1001; /* Home */
					case '8': return 1002; /* End */
					}
				}
			} else {
				switch (seq[1]) {
				case 'A': return 1005; /* Up */
				case 'B': return 1006; /* Down */
				case 'C': return 1007; /* Right */
				case 'D': return 1008; /* Left */
				case 'H': return 1001; /* Home */
				case 'F': return 1002; /* End */
				}
			}
		} else if (seq[0] == 'O') {
			switch (seq[1]) {
			case 'H': return 1001;
			case 'F': return 1002;
			}
		}
		return 27;
	}
	return (unsigned char)c;
}

/* -------------------------------------------------------------------------
 * Network Client
 * ------------------------------------------------------------------------- */

static int gopher_connect(const char *host, int port)
{
	struct hostent *he;
	struct sockaddr_in sa;
	int fd;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;

	he = gethostbyname(host);
	if (!he) {
		close(fd);
		return -1;
	}

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	memcpy(&sa.sin_addr, he->h_addr, he->h_length);

	alarm(6); /* 6 second timeout */
	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		alarm(0);
		close(fd);
		return -1;
	}
	alarm(0);
	return fd;
}

/* -------------------------------------------------------------------------
 * Gopher Menu Parsing
 * ------------------------------------------------------------------------- */

static void free_menu(struct GopherMenu *menu)
{
	int i;
	if (!menu)
		return;
	if (menu->host) free(menu->host);
	if (menu->selector) free(menu->selector);
	if (menu->title) free(menu->title);
	if (menu->items) {
		for (i = 0; i < menu->count; i++) {
			if (menu->items[i].display) free(menu->items[i].display);
			if (menu->items[i].selector) free(menu->items[i].selector);
			if (menu->items[i].host) free(menu->items[i].host);
		}
		free(menu->items);
	}
	memset(menu, 0, sizeof(*menu));
}

static struct GopherMenu *fetch_menu(const char *host, int port, const char *selector, const char *query)
{
	struct GopherMenu *m;
	int fd, n;
	char req[1024];
	char line[2048];
	int line_pos = 0;
	char c;

	fd = gopher_connect(host, port);
	if (fd < 0)
		return NULL;

	if (query && query[0]) {
		snprintf(req, sizeof(req), "%s\t%s\r\n", selector ? selector : "", query);
	} else {
		snprintf(req, sizeof(req), "%s\r\n", selector ? selector : "");
	}
	write(fd, req, strlen(req));

	m = calloc(1, sizeof(*m));
	m->host = strdup(host);
	m->port = port;
	m->selector = strdup(selector ? selector : "");
	m->title = strdup(host);
	m->capacity = 64;
	m->items = malloc(m->capacity * sizeof(struct GopherItem));
	m->count = 0;

	while ((n = read(fd, &c, 1)) > 0) {
		if (c == '\r') continue;
		if (c == '\n') {
			line[line_pos] = '\0';
			if (line[0] == '.' && line[1] == '\0') {
				break; /* End of gopher transmission */
			}
			if (line_pos > 0) {
				char *fields[5];
				int fcount = 0;
				char *tok = line + 1;
				char type = line[0];

				fields[fcount++] = tok;
				while (*tok) {
					if (*tok == '\t') {
						*tok = '\0';
						if (fcount < 5)
							fields[fcount++] = tok + 1;
					}
					tok++;
				}

				if (m->count >= m->capacity) {
					m->capacity *= 2;
					m->items = realloc(m->items, m->capacity * sizeof(struct GopherItem));
				}

				m->items[m->count].type = type;
				m->items[m->count].display = strdup(fields[0]);
				m->items[m->count].selector = (fcount > 1) ? strdup(fields[1]) : strdup("");
				m->items[m->count].host = (fcount > 2 && fields[2][0]) ? strdup(fields[2]) : strdup(host);
				m->items[m->count].port = (fcount > 3 && atoi(fields[3]) > 0) ? atoi(fields[3]) : port;

				/* Determine if the item is navigable / selectable */
				if (type == TYPE_INFO)
					m->items[m->count].is_navigable = 0;
				else
					m->items[m->count].is_navigable = 1;

				m->count++;
			}
			line_pos = 0;
		} else if (line_pos < (int)sizeof(line) - 1) {
			line[line_pos++] = c;
		}
	}
	close(fd);
	return m;
}

/* -------------------------------------------------------------------------
 * History Stack
 * ------------------------------------------------------------------------- */

static void push_history(const char *host, int port, const char *selector, const char *title, int cursor, int top)
{
	struct HistoryNode *node = malloc(sizeof(*node));
	node->host = strdup(host);
	node->port = port;
	node->selector = strdup(selector);
	node->title = strdup(title ? title : host);
	node->cursor = cursor;
	node->top = top;
	node->next = history_stack;
	history_stack = node;
}

static int pop_history(char **host, int *port, char **selector, char **title, int *cursor, int *top)
{
	struct HistoryNode *node;
	if (!history_stack)
		return 0;

	node = history_stack;
	history_stack = node->next;

	*host = node->host;
	*port = node->port;
	*selector = node->selector;
	*title = node->title;
	*cursor = node->cursor;
	*top = node->top;
	free(node);
	return 1;
}

/* -------------------------------------------------------------------------
 * Built-In Text Document Pager
 * ------------------------------------------------------------------------- */

static void view_text_document(const char *host, int port, const char *selector, const char *title)
{
	int fd, n, cap = 256, count = 0;
	char **lines = malloc(cap * sizeof(char *));
	char req[1024];
	char line_buf[2048];
	int pos = 0;
	char c;
	int top = 0;
	int key;

	fd = gopher_connect(host, port);
	if (fd < 0) {
		free(lines);
		return;
	}

	snprintf(req, sizeof(req), "%s\r\n", selector ? selector : "");
	write(fd, req, strlen(req));

	while ((n = read(fd, &c, 1)) > 0) {
		if (c == '\r') continue;
		if (c == '\n') {
			line_buf[pos] = '\0';
			if (line_buf[0] == '.' && line_buf[1] == '\0')
				break;
			if (count >= cap) {
				cap *= 2;
				lines = realloc(lines, cap * sizeof(char *));
			}
			lines[count++] = strdup(line_buf);
			pos = 0;
		} else if (pos < (int)sizeof(line_buf) - 1) {
			line_buf[pos++] = c;
		}
	}
	close(fd);

	if (pos > 0) {
		line_buf[pos] = '\0';
		if (count >= cap) {
			cap += 16;
			lines = realloc(lines, cap * sizeof(char *));
		}
		lines[count++] = strdup(line_buf);
	}

	while (1) {
		int i;
		int page_size = term_rows - 3;
		if (page_size < 1) page_size = 1;

		update_winsize();

		/* Header */
		printf("\033[H\033[7m  GOPHER TEXT: %-50.50s [Lines: %d] \033[0m\033[K\r\n",
		       title ? title : selector, count);

		/* Text body */
		for (i = 0; i < page_size; i++) {
			int idx = top + i;
			printf("\033[%d;1H\033[K", i + 2);
			if (idx < count) {
				char truncated[512];
				snprintf(truncated, sizeof(truncated), "%-.*s", term_cols, lines[idx]);
				printf("%s\r\n", truncated);
			} else {
				printf("~\r\n");
			}
		}

		/* Status bar */
		printf("\033[%d;1H\033[7m Line %d/%d (%d%%) | [Space/j/k] Scroll  [q/u] Return to Menu  [s] Save \033[0m\033[K",
		       term_rows, top + 1, count > 0 ? count : 1,
		       count > 0 ? ((top + page_size >= count ? 100 : (top * 100) / count)) : 100);
		fflush(stdout);

		key = read_key();
		if (key == 'q' || key == 'u' || key == 1008 || key == 27) {
			break;
		} else if (key == 'j' || key == 1006) { /* Down */
			if (top + page_size < count) top++;
		} else if (key == 'k' || key == 1005) { /* Up */
			if (top > 0) top--;
		} else if (key == ' ' || key == 1004) { /* Space / PageDown */
			top += page_size - 2;
			if (top + page_size > count) top = count - page_size;
			if (top < 0) top = 0;
		} else if (key == 'b' || key == 1003) { /* PageUp */
			top -= page_size - 2;
			if (top < 0) top = 0;
		} else if (key == 1001) { /* Home */
			top = 0;
		} else if (key == 1002) { /* End */
			top = count - page_size;
			if (top < 0) top = 0;
		} else if (key == 's') { /* Save file */
			char save_path[128];
			FILE *fp;
			term_restore();
			printf("\nSave document to filename: ");
			fflush(stdout);
			if (fgets(save_path, sizeof(save_path), stdin)) {
				save_path[strcspn(save_path, "\r\n")] = '\0';
				if (save_path[0]) {
					fp = fopen(save_path, "w");
					if (fp) {
						for (i = 0; i < count; i++) {
							fprintf(fp, "%s\n", lines[i]);
						}
						fclose(fp);
						printf("Document saved to %s! Press return to continue...", save_path);
					} else {
						printf("Error opening file! Press return to continue...");
					}
					getchar();
				}
			}
			term_raw();
		}
	}

	for (n = 0; n < count; n++) free(lines[n]);
	free(lines);
}

/* -------------------------------------------------------------------------
 * Interactive UI & Navigation Loop
 * ------------------------------------------------------------------------- */

static void render_menu(struct GopherMenu *menu, int cursor, int top)
{
	int i;
	int visible_rows = term_rows - 4;
	if (visible_rows < 1) visible_rows = 1;

	update_winsize();

	/* Top Title Banner */
	printf("\033[H\033[7m  GOPHER // %s:%d%s \033[0m\033[K\r\n",
	       menu->host, menu->port, menu->selector);
	printf("\033[2;1H\033[1m  Items: %d  (Use Up/Down or j/k to move, Enter to open)\033[0m\033[K\r\n",
	       menu->count);

	/* Menu Viewport */
	for (i = 0; i < visible_rows; i++) {
		int idx = top + i;
		printf("\033[%d;1H\033[K", i + 3);

		if (idx < menu->count) {
			struct GopherItem *item = &menu->items[idx];
			char badge[10] = "      ";
			char prefix[8] = "   ";
			int is_selected = (idx == cursor);

			if (item->type == TYPE_DIR)
				strcpy(badge, "[DIR] ");
			else if (item->type == TYPE_TEXT)
				strcpy(badge, "[TXT] ");
			else if (item->type == TYPE_SEARCH)
				strcpy(badge, "[ ? ] ");
			else if (item->type == TYPE_HTML)
				strcpy(badge, "[URL] ");
			else if (item->type == TYPE_BIN || item->type == TYPE_DOS)
				strcpy(badge, "[BIN] ");
			else if (item->type == TYPE_INFO)
				strcpy(badge, "      ");
			else
				strcpy(badge, "[OTH] ");

			if (is_selected)
				strcpy(prefix, "-> ");

			if (is_selected) {
				printf("\033[7m%s%s%-.*s\033[0m\r\n",
				       prefix, badge, term_cols - 12, item->display);
			} else {
				if (item->type == TYPE_DIR)
					printf("%s\033[1;34m%s\033[0m%-.*s\r\n", prefix, badge, term_cols - 12, item->display);
				else if (item->type == TYPE_TEXT)
					printf("%s\033[1;32m%s\033[0m%-.*s\r\n", prefix, badge, term_cols - 12, item->display);
				else if (item->type == TYPE_SEARCH)
					printf("%s\033[1;35m%s\033[0m%-.*s\r\n", prefix, badge, term_cols - 12, item->display);
				else if (item->type == TYPE_INFO)
					printf("%s      %-.*s\r\n", prefix, term_cols - 12, item->display);
				else
					printf("%s%s%-.*s\r\n", prefix, badge, term_cols - 12, item->display);
			}
		} else {
			printf("~\r\n");
		}
	}

	/* Bottom Status Bar */
	printf("\033[%d;1H\033[7m [Enter] Open  [u/Left] Back  [g] Go URL  [s] Save  [r] Reload  [q] Quit \033[0m\033[K",
	       term_rows);
	fflush(stdout);
}

static void gopher_loop(const char *initial_host, int initial_port, const char *initial_sel)
{
	char cur_host[128];
	char cur_sel[512];
	int cur_port = initial_port;
	struct GopherMenu *menu = NULL;
	int cursor = 0;
	int top = 0;

	strncpy(cur_host, initial_host, sizeof(cur_host) - 1);
	strncpy(cur_sel, initial_sel ? initial_sel : "", sizeof(cur_sel) - 1);

	term_raw();

	menu = fetch_menu(cur_host, cur_port, cur_sel, NULL);
	if (!menu || menu->count == 0) {
		term_restore();
		fprintf(stderr, "gopher: could not connect to %s:%d\n", cur_host, cur_port);
		return;
	}
	while (cursor < menu->count - 1 && !menu->items[cursor].is_navigable)
		cursor++;

	while (1) {
		int visible_rows = term_rows - 4;
		int key;

		if (visible_rows < 1) visible_rows = 1;

		/* Scroll top window to track cursor */
		if (cursor < top)
			top = cursor;
		if (cursor >= top + visible_rows)
			top = cursor - visible_rows + 1;

		render_menu(menu, cursor, top);

		key = read_key();

		if (key == 'q') {
			break;
		} else if (key == 'j' || key == 1006) { /* Down */
			if (cursor < menu->count - 1) {
				cursor++;
				while (cursor < menu->count - 1 && !menu->items[cursor].is_navigable)
					cursor++;
			}
		} else if (key == 'k' || key == 1005) { /* Up */
			if (cursor > 0) {
				cursor--;
				while (cursor > 0 && !menu->items[cursor].is_navigable)
					cursor--;
			}
		} else if (key == 1004 || key == ' ') { /* PageDown */
			cursor += visible_rows - 1;
			if (cursor >= menu->count) cursor = menu->count - 1;
		} else if (key == 1003 || key == 'b') { /* PageUp */
			cursor -= visible_rows - 1;
			if (cursor < 0) cursor = 0;
		} else if (key == 1001) { /* Home */
			cursor = 0;
		} else if (key == 1002) { /* End */
			cursor = menu->count - 1;
		} else if (key == '\r' || key == '\n' || key == 1007) { /* Enter or Right */
			if (cursor >= 0 && cursor < menu->count) {
				struct GopherItem *item = &menu->items[cursor];
				if (item->type == TYPE_DIR) {
					struct GopherMenu *new_menu;
					push_history(cur_host, cur_port, cur_sel, menu->title, cursor, top);
					new_menu = fetch_menu(item->host, item->port, item->selector, NULL);
					if (new_menu) {
						free_menu(menu);
						free(menu);
						menu = new_menu;
						strncpy(cur_host, item->host, sizeof(cur_host) - 1);
						cur_port = item->port;
						strncpy(cur_sel, item->selector, sizeof(cur_sel) - 1);
						cursor = 0;
						while (cursor < menu->count - 1 && !menu->items[cursor].is_navigable)
							cursor++;
						top = 0;
					} else {
						/* Pop back if failed */
						pop_history(&menu->host, &menu->port, &menu->selector, &menu->title, &cursor, &top);
					}
				} else if (item->type == TYPE_TEXT) {
					view_text_document(item->host, item->port, item->selector, item->display);
				} else if (item->type == TYPE_SEARCH) {
					char query[128];
					struct GopherMenu *new_menu;
					term_restore();
					printf("\nEnter search query for [%s]: ", item->display);
					fflush(stdout);
					if (fgets(query, sizeof(query), stdin)) {
						query[strcspn(query, "\r\n")] = '\0';
						term_raw();
						if (query[0]) {
							push_history(cur_host, cur_port, cur_sel, menu->title, cursor, top);
							new_menu = fetch_menu(item->host, item->port, item->selector, query);
							if (new_menu) {
								free_menu(menu);
								free(menu);
								menu = new_menu;
								strncpy(cur_host, item->host, sizeof(cur_host) - 1);
								cur_port = item->port;
								cursor = 0;
								top = 0;
							} else {
								pop_history(&menu->host, &menu->port, &menu->selector, &menu->title, &cursor, &top);
							}
						}
					} else {
						term_raw();
					}
				} else if (item->type == TYPE_HTML) {
					term_restore();
					printf("\nHTML Web Link: %s (Selector: %s)\nPress return to continue...",
					       item->display, item->selector);
					getchar();
					term_raw();
				}
			}
		} else if (key == 'u' || key == 1008) { /* Back */
			char *h, *s, *t;
			int p, c, tp;
			if (pop_history(&h, &p, &s, &t, &c, &tp)) {
				struct GopherMenu *prev_menu = fetch_menu(h, p, s, NULL);
				if (prev_menu) {
					free_menu(menu);
					free(menu);
					menu = prev_menu;
					strncpy(cur_host, h, sizeof(cur_host) - 1);
					cur_port = p;
					strncpy(cur_sel, s, sizeof(cur_sel) - 1);
					cursor = c;
					top = tp;
				}
				free(h); free(s); free(t);
			}
		} else if (key == 'r') { /* Reload */
			struct GopherMenu *reloaded = fetch_menu(cur_host, cur_port, cur_sel, NULL);
			if (reloaded) {
				free_menu(menu);
				free(menu);
				menu = reloaded;
			}
		} else if (key == 'g') { /* Go to URL */
			char url_input[128];
			term_restore();
			printf("\nEnter Gopher host [e.g. sdf.org or gopher.club]: ");
			fflush(stdout);
			if (fgets(url_input, sizeof(url_input), stdin)) {
				url_input[strcspn(url_input, "\r\n")] = '\0';
				if (url_input[0]) {
					struct GopherMenu *new_menu = fetch_menu(url_input, GOPHER_PORT, "", NULL);
					if (new_menu) {
						push_history(cur_host, cur_port, cur_sel, menu->title, cursor, top);
						free_menu(menu);
						free(menu);
						menu = new_menu;
						strncpy(cur_host, url_input, sizeof(cur_host) - 1);
						cur_port = GOPHER_PORT;
						cur_sel[0] = '\0';
						cursor = 0;
						top = 0;
					}
				}
			}
			term_raw();
		}
	}

	term_restore();
	if (menu) {
		free_menu(menu);
		free(menu);
	}
}

int main(int argc, char **argv)
{
	const char *host = DEFAULT_HOST;
	int port = GOPHER_PORT;
	const char *selector = "";

	if (argc > 1) {
		if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
			printf("Usage: gopher [host] [port] [selector]\n");
			printf("Default: gopher %s %d\n", DEFAULT_HOST, GOPHER_PORT);
			return 0;
		}
		host = argv[1];
	}
	if (argc > 2)
		port = atoi(argv[2]);
	if (argc > 3)
		selector = argv[3];

	gopher_loop(host, port, selector);
	return 0;
}
