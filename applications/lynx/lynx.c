/*
 * Lynx - Minimalist Text-Mode Web Browser for SIX
 *
 * Supports HTTP/1.0 GET, HTML rendering, interactive navigation,
 * link following, history, and -dump mode.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <linux/termios.h>
#include <stat.h>
#include <linux/fcntl.h>
#include <linux/types.h>
#include <linux/time.h>

extern int close(int fd);
extern int read(int fd, void *buf, size_t count);
extern int write(int fd, const void *buf, size_t count);
extern int tcgetattr(int fd, struct termios *termios_p);
extern int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);
extern int ioctl(int fd, int request, ...);
extern int open(const char *pathname, int flags, ...);
extern int select(int n, fd_set *inp, fd_set *outp, fd_set *exp, struct timeval *tvp);

#define MAX_LINES 2000
#define MAX_LINE_LEN 128
#define MAX_LINKS 128
#define MAX_URL_LEN 256
#define MAX_HIST 32

struct link_entry {
	int line;
	int col;
	int len;
	char url[MAX_URL_LEN];
};

static char doc_lines[MAX_LINES][MAX_LINE_LEN];
static int total_lines = 0;
static struct link_entry links[MAX_LINKS];
static int total_links = 0;
static char doc_title[128];
static char current_url[MAX_URL_LEN];

static char history_stack[MAX_HIST][MAX_URL_LEN];
static int history_count = 0;

static struct termios orig_termios;
static int raw_mode_set = 0;

static void enable_raw_mode(void)
{
	struct termios raw;
	if (tcgetattr(0, &orig_termios) == 0) {
		raw = orig_termios;
		raw.c_lflag &= ~(ECHO | ICANON | ISIG);
		raw.c_iflag &= ~(IXON | ICRNL);
		raw.c_cc[VMIN] = 1;
		raw.c_cc[VTIME] = 0;
		tcsetattr(0, TCSANOW, &raw);
		raw_mode_set = 1;
	}
}

static void disable_raw_mode(void)
{
	if (raw_mode_set) {
		tcsetattr(0, TCSANOW, &orig_termios);
		raw_mode_set = 0;
	}
}

static void get_screen_size(int *rows, int *cols)
{
	struct winsize ws;
	if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_row && ws.ws_col) {
		*rows = ws.ws_row;
		*cols = ws.ws_col;
	} else {
		*rows = 24;
		*cols = 80;
	}
}

static int parse_url(const char *url, char *host, int *port, char *path)
{
	const char *p = url;
	char *h = host;
	char *pa = path;

	if (strncmp(p, "http://", 7) == 0)
		p += 7;

	*port = 80;
	while (*p && *p != ':' && *p != '/') {
		*h++ = *p++;
	}
	*h = '\0';

	if (*p == ':') {
		p++;
		*port = atoi(p);
		while (*p && *p != '/') p++;
	}

	if (*p == '/') {
		while (*p) *pa++ = *p++;
		*pa = '\0';
	} else {
		strcpy(path, "/");
	}
	return 0;
}

static char *http_fetch(const char *url, int *out_len)
{
	char host[128];
	int port;
	char path[MAX_URL_LEN];
	struct hostent *he;
	struct sockaddr_in saddr;
	int sfd;
	char req[512];
	char *buf;
	int buf_size = 32768;
	int total = 0;
	int r;

	if (strncmp(url, "file://", 7) == 0 || url[0] == '/') {
		const char *fpath = (strncmp(url, "file://", 7) == 0) ? url + 7 : url;
		int fd = open(fpath, O_RDONLY);
		if (fd < 0) return NULL;
		buf = malloc(buf_size);
		if (!buf) { close(fd); return NULL; }
		while ((r = read(fd, buf + total, buf_size - total - 1)) > 0) {
			total += r;
			if (total >= buf_size - 1024) {
				buf_size *= 2;
				buf = realloc(buf, buf_size);
			}
		}
		close(fd);
		buf[total] = '\0';
		*out_len = total;
		return buf;
	}

	parse_url(url, host, &port, path);

	he = gethostbyname(host);
	if (!he) {
		printf("lynx: unable to resolve host '%s'\n", host);
		return NULL;
	}

	sfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sfd < 0) {
		printf("lynx: socket() failed\n");
		return NULL;
	}

	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons((unsigned short)port);
	memcpy(&saddr.sin_addr, he->h_addr_list[0], he->h_length);

	if (connect(sfd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
		printf("lynx: connect to %s:%d failed\n", host, port);
		close(sfd);
		return NULL;
	}

	sprintf(req, "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\nUser-Agent: SIX/1.0 (Linux 2.0.11)\r\nAccept: text/html, text/plain, */*\r\n\r\n", path, host);
	write(sfd, req, strlen(req));

	buf = malloc(buf_size);
	if (!buf) {
		close(sfd);
		return NULL;
	}

	int content_len = -1;
	int header_len = 0;
	for (;;) {
		struct timeval tv;
		tv.tv_sec = (total == 0) ? 5 : 2;
		tv.tv_usec = 0;
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(sfd, &rfds);

		if (select(sfd + 1, &rfds, NULL, NULL, &tv) <= 0) {
			/* Timeout - break if we received headers, else fail */
			if (total > 0 && strstr(buf, "\r\n\r\n")) break;
			if (total == 0) {
				close(sfd);
				free(buf);
				return NULL;
			}
			break;
		}

		r = read(sfd, buf + total, buf_size - total - 1);
		if (r <= 0)
			break;
		total += r;
		buf[total] = '\0';
		if (content_len < 0) {
			char *hdr_end = strstr(buf, "\r\n\r\n");
			if (hdr_end) {
				header_len = (hdr_end - buf) + 4;
				char *cl = strstr(buf, "Content-Length:");
				if (!cl) cl = strstr(buf, "content-length:");
				if (cl && cl < hdr_end) {
					cl += 15;
					while (*cl == ' ') cl++;
					content_len = atoi(cl);
				}
			}
		}
		if (content_len >= 0 && total >= header_len + content_len)
			break;
		if (total >= buf_size - 1024) {
			buf_size *= 2;
			buf = realloc(buf, buf_size);
		}
	}
	close(sfd);
	buf[total] = '\0';

	/* Strip HTTP headers if present */
	{
		char *body = strstr(buf, "\r\n\r\n");
		if (body) {
			body += 4;
			int blen = total - (body - buf);
			char *newbuf = malloc(blen + 1);
			memcpy(newbuf, body, blen);
			newbuf[blen] = '\0';
			free(buf);
			*out_len = blen;
			return newbuf;
		}
	}
	*out_len = total;
	return buf;
}

static void resolve_link(const char *base, const char *href, char *out, size_t out_sz)
{
	char host[128], path[MAX_URL_LEN];
	int port;

	if (strncmp(href, "http://", 7) == 0 || strncmp(href, "file://", 7) == 0) {
		strncpy(out, href, out_sz - 1);
		out[out_sz - 1] = '\0';
		return;
	}

	parse_url(base, host, &port, path);

	if (href[0] == '/') {
		if (port == 80)
			sprintf(out, "http://%s%s", host, href);
		else
			sprintf(out, "http://%s:%d%s", host, port, href);
	} else {
		char *slash = strrchr(path, '/');
		if (slash) *(slash + 1) = '\0';
		else strcpy(path, "/");

		if (port == 80)
			sprintf(out, "http://%s%s%s", host, path, href);
		else
			sprintf(out, "http://%s:%d%s%s", host, port, path, href);
	}
	out[out_sz - 1] = '\0';
}

static void add_line(const char *line)
{
	if (total_lines < MAX_LINES) {
		strncpy(doc_lines[total_lines], line, MAX_LINE_LEN - 1);
		doc_lines[total_lines][MAX_LINE_LEN - 1] = '\0';
		total_lines++;
	}
}

static void render_html(const char *html)
{
	const char *p = html;
	char cur_line[MAX_LINE_LEN];
	int cur_col = 0;
	int in_tag = 0;
	char tag_buf[128];
	int tag_idx = 0;
	int in_pre = 0;
	int in_style = 0;
	int in_script = 0;
	int in_title = 0;
	char title_buf[128];
	int title_idx = 0;
	char cur_href[MAX_URL_LEN];
	int in_a = 0;

	total_lines = 0;
	total_links = 0;
	doc_title[0] = '\0';
	cur_line[0] = '\0';
	cur_href[0] = '\0';

	while (*p) {
		if (*p == '<') {
			in_tag = 1;
			tag_idx = 0;
			p++;
			continue;
		}
		if (in_tag) {
			if (*p == '>') {
				in_tag = 0;
				tag_buf[tag_idx] = '\0';

				/* Process tag */
				if (strcasecmp(tag_buf, "title") == 0) {
					in_title = 1;
					title_idx = 0;
				} else if (strcasecmp(tag_buf, "/title") == 0) {
					in_title = 0;
					title_buf[title_idx] = '\0';
					strncpy(doc_title, title_buf, sizeof(doc_title) - 1);
				} else if (strncasecmp(tag_buf, "h1", 2) == 0 ||
					   strncasecmp(tag_buf, "h2", 2) == 0 ||
					   strncasecmp(tag_buf, "h3", 2) == 0) {
					if (cur_col > 0) { add_line(cur_line); cur_col = 0; cur_line[0] = '\0'; }
					add_line("");
				} else if (strncasecmp(tag_buf, "/h1", 3) == 0 ||
					   strncasecmp(tag_buf, "/h2", 3) == 0 ||
					   strncasecmp(tag_buf, "/h3", 3) == 0) {
					if (cur_col > 0) { add_line(cur_line); cur_col = 0; cur_line[0] = '\0'; }
					add_line("");
				} else if (strcasecmp(tag_buf, "p") == 0 || strcasecmp(tag_buf, "/p") == 0) {
					if (cur_col > 0) { add_line(cur_line); cur_col = 0; cur_line[0] = '\0'; }
					add_line("");
				} else if (strcasecmp(tag_buf, "br") == 0) {
					add_line(cur_line);
					cur_col = 0;
					cur_line[0] = '\0';
				} else if (strcasecmp(tag_buf, "hr") == 0) {
					if (cur_col > 0) { add_line(cur_line); cur_col = 0; cur_line[0] = '\0'; }
					add_line("--------------------------------------------------------------------------------");
				} else if (strcasecmp(tag_buf, "li") == 0) {
					if (cur_col > 0) { add_line(cur_line); cur_col = 0; cur_line[0] = '\0'; }
					strcpy(cur_line, "  * ");
					cur_col = 4;
				} else if (strcasecmp(tag_buf, "pre") == 0) {
					in_pre = 1;
					if (cur_col > 0) { add_line(cur_line); cur_col = 0; cur_line[0] = '\0'; }
				} else if (strcasecmp(tag_buf, "/pre") == 0) {
					in_pre = 0;
					if (cur_col > 0) { add_line(cur_line); cur_col = 0; cur_line[0] = '\0'; }
				} else if (strncasecmp(tag_buf, "style", 5) == 0) {
					in_style = 1;
				} else if (strncasecmp(tag_buf, "/style", 6) == 0) {
					in_style = 0;
				} else if (strncasecmp(tag_buf, "script", 6) == 0) {
					in_script = 1;
				} else if (strncasecmp(tag_buf, "/script", 7) == 0) {
					in_script = 0;
				} else if (strncasecmp(tag_buf, "a ", 2) == 0) {
					char *h = strstr(tag_buf, "href=");
					if (h) {
						h += 5;
						if (*h == '"' || *h == '\'') h++;
						char *end = h;
						while (*end && *end != '"' && *end != '\'' && *end != ' ' && *end != '>') end++;
						*end = '\0';
						resolve_link(current_url, h, cur_href, sizeof(cur_href));
						in_a = 1;
					}
				} else if (strcasecmp(tag_buf, "/a") == 0) {
					if (in_a && total_links < MAX_LINKS) {
						char num[16];
						sprintf(num, "[%d]", total_links + 1);
						int nlen = strlen(num);
						if (cur_col + nlen < MAX_LINE_LEN - 1) {
							strcpy(cur_line + cur_col, num);
							cur_col += nlen;
							cur_line[cur_col] = '\0';
						}
						links[total_links].line = total_lines;
						links[total_links].col = cur_col;
						links[total_links].len = nlen;
						strncpy(links[total_links].url, cur_href, MAX_URL_LEN - 1);
						total_links++;
					}
					in_a = 0;
				}
				p++;
				continue;
			}
			if (tag_idx < sizeof(tag_buf) - 1)
				tag_buf[tag_idx++] = *p;
			p++;
			continue;
		}

		if (in_style || in_script) {
			p++;
			continue;
		}

		if (in_title) {
			if (title_idx < sizeof(title_buf) - 1)
				title_buf[title_idx++] = *p;
			p++;
			continue;
		}

		if (in_pre) {
			if (*p == '\n') {
				add_line(cur_line);
				cur_col = 0;
				cur_line[0] = '\0';
			} else {
				if (cur_col < MAX_LINE_LEN - 1) {
					cur_line[cur_col++] = *p;
					cur_line[cur_col] = '\0';
				}
			}
			p++;
			continue;
		}

		/* Regular text flow */
		if (*p == '\n' || *p == '\r' || *p == '\t') {
			if (cur_col > 0 && cur_line[cur_col - 1] != ' ') {
				cur_line[cur_col++] = ' ';
				cur_line[cur_col] = '\0';
			}
			p++;
			continue;
		}

		/* Entity decoding */
		if (*p == '&') {
			if (strncmp(p, "&lt;", 4) == 0) { cur_line[cur_col++] = '<'; p += 4; }
			else if (strncmp(p, "&gt;", 4) == 0) { cur_line[cur_col++] = '>'; p += 4; }
			else if (strncmp(p, "&amp;", 5) == 0) { cur_line[cur_col++] = '&'; p += 5; }
			else if (strncmp(p, "&quot;", 6) == 0) { cur_line[cur_col++] = '"'; p += 6; }
			else if (strncmp(p, "&nbsp;", 6) == 0) { cur_line[cur_col++] = ' '; p += 6; }
			else { cur_line[cur_col++] = *p++; }
			cur_line[cur_col] = '\0';
		} else {
			cur_line[cur_col++] = *p++;
			cur_line[cur_col] = '\0';
		}

		if (cur_col >= 75 && cur_line[cur_col - 1] == ' ') {
			add_line(cur_line);
			cur_col = 0;
			cur_line[0] = '\0';
		}
	}
	if (cur_col > 0)
		add_line(cur_line);

	if (doc_title[0] == '\0')
		strcpy(doc_title, "SIX Lynx");
}

static void dump_mode(const char *url)
{
	int len;
	char *html = http_fetch(url, &len);
	if (!html) {
		printf("lynx: failed to load %s\n", url);
		return;
	}
	strncpy(current_url, url, sizeof(current_url) - 1);
	render_html(html);
	free(html);

	int i;
	for (i = 0; i < total_lines; i++) {
		printf("%s\n", doc_lines[i]);
	}

	if (total_links > 0) {
		printf("\nReferences:\n");
		for (i = 0; i < total_links; i++) {
			printf("  %2d. %s\n", i + 1, links[i].url);
		}
	}
}

static void draw_screen(int top_line, int cur_link, int rows, int cols)
{
	int i;
	printf("\033[H"); /* Move to home */

	/* Top Status Bar: Reverse Video */
	printf("\033[7m");
	printf(" Lynx 2.8.4: %-50.50s (%d/%d)", doc_title, top_line + 1, total_lines > 0 ? total_lines : 1);
	for (i = strlen(doc_title) + 20; i < cols; i++) putchar(' ');
	printf("\033[0m\r\n");

	/* Document Body */
	int view_rows = rows - 2;
	for (i = 0; i < view_rows; i++) {
		int line_idx = top_line + i;
		printf("\033[K"); /* Clear line */
		if (line_idx < total_lines) {
			/* Check if current line contains selected link */
			if (cur_link >= 0 && cur_link < total_links && links[cur_link].line == line_idx) {
				/* Print line with highlighted link */
				printf("\033[1;36m%s\033[0m", doc_lines[line_idx]);
			} else {
				printf("%s", doc_lines[line_idx]);
			}
		}
		printf("\r\n");
	}

	/* Bottom Command Bar: Reverse Video */
	printf("\033[7m");
	if (cur_link >= 0 && cur_link < total_links) {
		printf(" [%d/%d] %-40.40s  Enter: Follow  g: Go  b: Back  q: Quit",
		       cur_link + 1, total_links, links[cur_link].url);
	} else {
		printf(" Commands: Down/Up: Scroll  g: Go to URL  b: Back  q: Quit  h: Help");
	}
	for (i = 65; i < cols; i++) putchar(' ');
	printf("\033[0m");
	fflush(stdout);
}

static void interactive_browse(const char *initial_url)
{
	int top_line = 0;
	int cur_link = 0;
	int rows, cols;
	char url[MAX_URL_LEN];

	strncpy(url, initial_url, sizeof(url) - 1);

load_new_url:
	{
		int len;
		char *html;

		printf("\033[H\033[2JGetting %s...\r\n", url);
		fflush(stdout);

		strncpy(current_url, url, sizeof(current_url) - 1);
		html = http_fetch(url, &len);
		if (!html) {
			printf("\r\nFailed to load %s. Press any key to continue.\r\n", url);
			read(0, &rows, 1);
			return;
		}

		render_html(html);
		free(html);

		top_line = 0;
		cur_link = (total_links > 0) ? 0 : -1;
	}

	enable_raw_mode();
	printf("\033[?25l"); /* Hide cursor */

	for (;;) {
		get_screen_size(&rows, &cols);
		draw_screen(top_line, cur_link, rows, cols);

		char ch;
		if (read(0, &ch, 1) <= 0) break;

		if (ch == 'q' || ch == 'Q') {
			break;
		} else if (ch == '\033') {
			/* Escape sequence: arrows */
			char seq[3];
			if (read(0, &seq[0], 1) > 0 && read(0, &seq[1], 1) > 0) {
				if (seq[0] == '[') {
					if (seq[1] == 'A') { /* Up */
						if (cur_link > 0) cur_link--;
						else if (top_line > 0) top_line--;
					} else if (seq[1] == 'B') { /* Down */
						if (cur_link < total_links - 1) cur_link++;
						else if (top_line + (rows - 2) < total_lines) top_line++;
					} else if (seq[1] == '5' && read(0, &seq[2], 1) > 0) { /* PgUp */
						top_line -= (rows - 2);
						if (top_line < 0) top_line = 0;
					} else if (seq[1] == '6' && read(0, &seq[2], 1) > 0) { /* PgDn */
						if (top_line + (rows - 2) < total_lines)
							top_line += (rows - 2);
					}
				}
			}
		} else if (ch == 'j') {
			if (cur_link < total_links - 1) cur_link++;
			else if (top_line + (rows - 2) < total_lines) top_line++;
		} else if (ch == 'k') {
			if (cur_link > 0) cur_link--;
			else if (top_line > 0) top_line--;
		} else if (ch == ' ' || ch == 'f') {
			if (top_line + (rows - 2) < total_lines)
				top_line += (rows - 2);
		} else if (ch == 'b' || ch == 'B') {
			/* Back in history or scroll up */
			if (history_count > 0) {
				history_count--;
				strncpy(url, history_stack[history_count], sizeof(url) - 1);
				disable_raw_mode();
				printf("\033[?25h");
				goto load_new_url;
			} else {
				top_line -= (rows - 2);
				if (top_line < 0) top_line = 0;
			}
		} else if (ch == '\r' || ch == '\n') {
			/* Follow currently selected link */
			if (cur_link >= 0 && cur_link < total_links) {
				if (history_count < MAX_HIST) {
					strncpy(history_stack[history_count++], current_url, MAX_URL_LEN - 1);
				}
				strncpy(url, links[cur_link].url, sizeof(url) - 1);
				disable_raw_mode();
				printf("\033[?25h");
				goto load_new_url;
			}
		} else if (ch >= '1' && ch <= '9') {
			/* Jump to link number */
			int lidx = ch - '1';
			if (lidx < total_links) {
				if (history_count < MAX_HIST) {
					strncpy(history_stack[history_count++], current_url, MAX_URL_LEN - 1);
				}
				strncpy(url, links[lidx].url, sizeof(url) - 1);
				disable_raw_mode();
				printf("\033[?25h");
				goto load_new_url;
			}
		} else if (ch == 'g' || ch == 'G') {
			/* Go to URL prompt */
			char newurl[MAX_URL_LEN];
			disable_raw_mode();
			printf("\033[?25h\033[%d;1H\033[KURL to open: ", rows);
			fflush(stdout);
			if (fgets(newurl, sizeof(newurl), stdin)) {
				char *nl = strchr(newurl, '\n');
				if (nl) *nl = '\0';
				nl = strchr(newurl, '\r');
				if (nl) *nl = '\0';
				if (strlen(newurl) > 0) {
					if (history_count < MAX_HIST) {
						strncpy(history_stack[history_count++], current_url, MAX_URL_LEN - 1);
					}
					strncpy(url, newurl, sizeof(url) - 1);
					goto load_new_url;
				}
			}
			enable_raw_mode();
			printf("\033[?25l");
		} else if (ch == 'r' || ch == 'R') {
			disable_raw_mode();
			printf("\033[?25h");
			goto load_new_url;
		}

		/* Adjust top_line if cur_link moved outside view */
		if (cur_link >= 0 && cur_link < total_links) {
			int lline = links[cur_link].line;
			if (lline < top_line) top_line = lline;
			if (lline >= top_line + rows - 2) top_line = lline - (rows - 3);
			if (top_line < 0) top_line = 0;
		}
	}

	disable_raw_mode();
	printf("\033[?25h\033[H\033[2J"); /* Show cursor and clear screen */
}

int main(int argc, char **argv)
{
	const char *url = "http://127.0.0.1:80/index.html";
	int dump = 0;
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-dump") == 0) {
			dump = 1;
		} else if (argv[i][0] != '-') {
			url = argv[i];
		}
	}

	if (dump) {
		dump_mode(url);
		return 0;
	}

	interactive_browse(url);
	return 0;
}
