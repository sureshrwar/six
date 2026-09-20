/*
 * Lynx - Minimalist Text-Mode Web Browser for SIX
 *
 * Supports HTTP/1.0 & HTTPS (via built-in TLS bridge), HTML rendering,
 * interactive navigation, link following, history, redirects, in-page
 * search, source viewing, page saving, bookmarks, and -dump mode.
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

#define MAX_LINES 2500
#define MAX_LINE_LEN 128
#define MAX_LINKS 256
#define MAX_URL_LEN 512
#define MAX_HIST 32
#define HTTP_BUF_SIZE 262144

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

static char http_buf[HTTP_BUF_SIZE];
static char *raw_doc = NULL;
static int raw_doc_len = 0;
static int view_source_mode = 0;

static char last_search[64] = "";
static char status_msg[128] = "";

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

static int lynx_tolower(int c)
{
	if (c >= 'A' && c <= 'Z')
		return c + ('a' - 'A');
	return c;
}

static int lynx_strcasecmp(const char *s1, const char *s2)
{
	while (*s1 && *s2) {
		int c1 = lynx_tolower((unsigned char)*s1);
		int c2 = lynx_tolower((unsigned char)*s2);
		if (c1 != c2)
			return c1 - c2;
		s1++;
		s2++;
	}
	return lynx_tolower((unsigned char)*s1) - lynx_tolower((unsigned char)*s2);
}

static int lynx_strncasecmp(const char *s1, const char *s2, size_t n)
{
	while (n && *s1 && *s2) {
		int c1 = lynx_tolower((unsigned char)*s1);
		int c2 = lynx_tolower((unsigned char)*s2);
		if (c1 != c2)
			return c1 - c2;
		s1++;
		s2++;
		n--;
	}
	if (n == 0)
		return 0;
	return lynx_tolower((unsigned char)*s1) - lynx_tolower((unsigned char)*s2);
}

static char *lynx_strcasestr(const char *haystack, const char *needle)
{
	if (!needle || !*needle)
		return (char *)haystack;
	for (; *haystack; haystack++) {
		if (lynx_tolower((unsigned char)*haystack) == lynx_tolower((unsigned char)*needle)) {
			const char *h = haystack;
			const char *n = needle;
			while (*h && *n && lynx_tolower((unsigned char)*h) == lynx_tolower((unsigned char)*n)) {
				h++;
				n++;
			}
			if (!*n)
				return (char *)haystack;
		}
	}
	return NULL;
}

static int parse_url(const char *url, char *host, int *port, char *path, int *is_https)
{
	const char *p = url;
	char *h = host;
	char *pa = path;

	*is_https = 0;
	*port = 80;

	if (strncmp(p, "https://", 8) == 0) {
		p += 8;
		*is_https = 1;
		*port = 443;
	} else if (strncmp(p, "http://", 7) == 0) {
		p += 7;
		*is_https = 0;
		*port = 80;
	}

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

static void resolve_link(const char *base, const char *href, char *out, size_t out_sz)
{
	char host[128], path[MAX_URL_LEN];
	int port, is_https;
	const char *proto;
	int def_port;

	if (strncmp(href, "http://", 7) == 0 ||
	    strncmp(href, "https://", 8) == 0 ||
	    strncmp(href, "file://", 7) == 0) {
		strncpy(out, href, out_sz - 1);
		out[out_sz - 1] = '\0';
		return;
	}

	parse_url(base, host, &port, path, &is_https);
	proto = is_https ? "https" : "http";
	def_port = is_https ? 443 : 80;

	if (href[0] == '/') {
		if (port == def_port)
			sprintf(out, "%s://%s%s", proto, host, href);
		else
			sprintf(out, "%s://%s:%d%s", proto, host, port, href);
	} else {
		char *slash = strrchr(path, '/');
		if (slash) *(slash + 1) = '\0';
		else strcpy(path, "/");

		if (port == def_port)
			sprintf(out, "%s://%s%s%s", proto, host, path, href);
		else
			sprintf(out, "%s://%s:%d%s%s", proto, host, port, path, href);
	}
	out[out_sz - 1] = '\0';
}

static char *http_fetch_internal(const char *url, int *out_len, int depth);

static char *http_fetch(const char *url, int *out_len)
{
	return http_fetch_internal(url, out_len, 0);
}

static char *http_fetch_internal(const char *url, int *out_len, int depth)
{
	char host[128];
	int port, is_https;
	char path[MAX_URL_LEN];
	struct sockaddr_in saddr;
	int sfd;
	static char req[1024];
	char *buf = http_buf;
	int buf_size = sizeof(http_buf);
	int total = 0;
	int r;

	if (depth > 5) {
		printf("lynx: too many redirects\n");
		return NULL;
	}

	if (strncmp(url, "file://", 7) == 0 || url[0] == '/') {
		const char *fpath = (strncmp(url, "file://", 7) == 0) ? url + 7 : url;
		int fd = open(fpath, O_RDONLY);
		if (fd < 0) return NULL;
		while ((r = read(fd, buf + total, buf_size - total - 1)) > 0) {
			total += r;
			if (total >= buf_size - 1) break;
		}
		close(fd);
		buf[total] = '\0';
		*out_len = total;
		return buf;
	}

	parse_url(url, host, &port, path, &is_https);

	if (is_https) {
		/* Connect to TLS bridge at 10.0.2.2:18443 (guest) or 127.0.0.1:18443 (host) */
		sfd = socket(AF_INET, SOCK_STREAM, 0);
		if (sfd < 0) {
			printf("lynx: socket() failed\n");
			return NULL;
		}
		memset(&saddr, 0, sizeof(saddr));
		saddr.sin_family = AF_INET;
		saddr.sin_port = htons(18443);
		saddr.sin_addr.s_addr = inet_addr("10.0.2.2");

		if (connect(sfd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
			close(sfd);
			sfd = socket(AF_INET, SOCK_STREAM, 0);
			saddr.sin_addr.s_addr = inet_addr("127.0.0.1");
			if (connect(sfd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
				printf("lynx: connect to TLS bridge failed\n");
				close(sfd);
				return NULL;
			}
		}

		/* Send CONNECT request */
		static char creq[256];
		sprintf(creq, "CONNECT %s:%d HTTP/1.0\r\nHost: %s:%d\r\n\r\n", host, port, host, port);
		write(sfd, creq, strlen(creq));

		/* Read CONNECT response */
		static char cresp[512];
		int cresplen = 0;
		while (cresplen < sizeof(cresp) - 1) {
			r = read(sfd, cresp + cresplen, 1);
			if (r <= 0) break;
			cresplen += r;
			cresp[cresplen] = '\0';
			if (strstr(cresp, "\r\n\r\n")) break;
		}
		if (!strstr(cresp, " 200 ")) {
			printf("lynx: TLS handshake refused: %s\n", cresp);
			close(sfd);
			return NULL;
		}
	} else {
		/* Standard HTTP: resolve host and connect */
		struct hostent *he = gethostbyname(host);
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
	}

	/* Send HTTP Request over the connection / TLS tunnel */
	sprintf(req, "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\nUser-Agent: SIX/1.0 (Linux 2.0.11)\r\nAccept: text/html, text/plain, */*\r\n\r\n", path, host);
	write(sfd, req, strlen(req));

	int content_len = -1;
	int header_len = 0;
	for (;;) {
		struct timeval tv;
		tv.tv_sec = (total == 0) ? 6 : 2;
		tv.tv_usec = 0;
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(sfd, &rfds);

		if (select(sfd + 1, &rfds, NULL, NULL, &tv) <= 0) {
			if (total > 0 && strstr(buf, "\r\n\r\n")) break;
			if (total == 0) {
				close(sfd);
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
		if (total >= buf_size - 1)
			break;
	}
	close(sfd);
	buf[total] = '\0';

	/* Check for HTTP Redirects (301, 302, 303, 307, 308) */
	if (strncmp(buf, "HTTP/1.", 7) == 0 && (buf[9] == '3')) {
		char *loc = strstr(buf, "Location:");
		if (!loc) loc = strstr(buf, "location:");
		if (loc) {
			static char redirect_url[MAX_URL_LEN];
			static char resolved_url[MAX_URL_LEN];
			int i = 0;
			loc += 9;
			while (*loc == ' ') loc++;
			while (*loc && *loc != '\r' && *loc != '\n' && i < MAX_URL_LEN - 1) {
				redirect_url[i++] = *loc++;
			}
			redirect_url[i] = '\0';
			resolve_link(url, redirect_url, resolved_url, sizeof(resolved_url));
			strncpy(current_url, resolved_url, sizeof(current_url) - 1);
			return http_fetch_internal(resolved_url, out_len, depth + 1);
		}
	}

	/* Strip HTTP response headers */
	{
		char *body = strstr(buf, "\r\n\r\n");
		if (body) {
			int blen, bi;
			body += 4;
			blen = total - (body - buf);
			for (bi = 0; bi < blen; bi++)
				buf[bi] = body[bi];
			buf[blen] = '\0';
			*out_len = blen;
			return buf;
		}
	}

	*out_len = total;
	return buf;
}

static void add_line(const char *line)
{
	if (total_lines < MAX_LINES) {
		strncpy(doc_lines[total_lines], line, MAX_LINE_LEN - 1);
		doc_lines[total_lines][MAX_LINE_LEN - 1] = '\0';
		total_lines++;
	}
}

static void add_html_blank_line(void)
{
	if (total_lines > 0 && doc_lines[total_lines - 1][0] != '\0') {
		add_line("");
	}
}

static void flush_html_line(char *cur_line, int *cur_col)
{
	if (*cur_col > 0) {
		int i, has_content = 0;
		for (i = 0; i < *cur_col; i++) {
			if (cur_line[i] != ' ' && cur_line[i] != '\t') {
				has_content = 1;
				break;
			}
		}
		if (has_content) {
			while (*cur_col > 0 && cur_line[*cur_col - 1] == ' ') {
				(*cur_col)--;
			}
			cur_line[*cur_col] = '\0';
			add_line(cur_line);
		}
		*cur_col = 0;
		cur_line[0] = '\0';
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
		if (in_script) {
			if (*p == '<' && lynx_strncasecmp(p, "</script", 8) == 0) {
				in_script = 0;
				while (*p && *p != '>') p++;
				if (*p == '>') p++;
			} else {
				p++;
			}
			continue;
		}

		if (in_style) {
			if (*p == '<' && lynx_strncasecmp(p, "</style", 7) == 0) {
				in_style = 0;
				while (*p && *p != '>') p++;
				if (*p == '>') p++;
			} else {
				p++;
			}
			continue;
		}

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

				if (lynx_strcasecmp(tag_buf, "title") == 0) {
					in_title = 1;
					title_idx = 0;
				} else if (lynx_strcasecmp(tag_buf, "/title") == 0) {
					in_title = 0;
					title_buf[title_idx] = '\0';
					strncpy(doc_title, title_buf, sizeof(doc_title) - 1);
				} else if (lynx_strncasecmp(tag_buf, "h1", 2) == 0 ||
					   lynx_strncasecmp(tag_buf, "h2", 2) == 0 ||
					   lynx_strncasecmp(tag_buf, "h3", 2) == 0 ||
					   lynx_strncasecmp(tag_buf, "/h1", 3) == 0 ||
					   lynx_strncasecmp(tag_buf, "/h2", 3) == 0 ||
					   lynx_strncasecmp(tag_buf, "/h3", 3) == 0) {
					flush_html_line(cur_line, &cur_col);
					add_html_blank_line();
				} else if (lynx_strcasecmp(tag_buf, "p") == 0 ||
					   lynx_strncasecmp(tag_buf, "p ", 2) == 0 ||
					   lynx_strcasecmp(tag_buf, "/p") == 0) {
					flush_html_line(cur_line, &cur_col);
					add_html_blank_line();
				} else if (lynx_strncasecmp(tag_buf, "div", 3) == 0 ||
					   lynx_strncasecmp(tag_buf, "/div", 4) == 0 ||
					   lynx_strncasecmp(tag_buf, "tr", 2) == 0 ||
					   lynx_strncasecmp(tag_buf, "/tr", 3) == 0) {
					flush_html_line(cur_line, &cur_col);
				} else if (lynx_strncasecmp(tag_buf, "br", 2) == 0) {
					flush_html_line(cur_line, &cur_col);
				} else if (lynx_strncasecmp(tag_buf, "hr", 2) == 0) {
					flush_html_line(cur_line, &cur_col);
					add_line("--------------------------------------------------------------------------------");
				} else if (lynx_strncasecmp(tag_buf, "li", 2) == 0) {
					flush_html_line(cur_line, &cur_col);
					strcpy(cur_line, "  * ");
					cur_col = 4;
				} else if (lynx_strncasecmp(tag_buf, "input", 5) == 0) {
					if (!strstr(tag_buf, "type=\"hidden\"") && !strstr(tag_buf, "type=hidden")) {
						char *v = strstr(tag_buf, "value=");
						if (v) {
							char val[64];
							int vi = 0;
							char qchar = 0;
							v += 6;
							if (*v == '"' || *v == '\'') qchar = *v++;
							while (*v && (qchar ? (*v != qchar) : (*v != ' ' && *v != '>')) && vi < 55) {
								val[vi++] = *v++;
							}
							val[vi] = '\0';
							if (vi > 0 && cur_col + vi + 5 < MAX_LINE_LEN - 1) {
								sprintf(cur_line + cur_col, " [ %s ] ", val);
								cur_col += vi + 5;
							}
						} else if (strstr(tag_buf, "type=\"text\"") || strstr(tag_buf, "type=text") ||
							   strstr(tag_buf, "name=\"q\"") || strstr(tag_buf, "name=q")) {
							if (cur_col + 27 < MAX_LINE_LEN - 1) {
								strcpy(cur_line + cur_col, " [________________________] ");
								cur_col += 28;
							}
						}
					}
				} else if (lynx_strcasecmp(tag_buf, "pre") == 0) {
					in_pre = 1;
					flush_html_line(cur_line, &cur_col);
				} else if (lynx_strcasecmp(tag_buf, "/pre") == 0) {
					in_pre = 0;
					flush_html_line(cur_line, &cur_col);
				} else if (lynx_strncasecmp(tag_buf, "style", 5) == 0) {
					in_style = 1;
				} else if (lynx_strncasecmp(tag_buf, "script", 6) == 0) {
					in_script = 1;
				} else if (lynx_strncasecmp(tag_buf, "a ", 2) == 0) {
					char *h = strstr(tag_buf, "href=");
					if (h) {
						char *end;
						h += 5;
						if (*h == '"' || *h == '\'') h++;
						end = h;
						while (*end && *end != '"' && *end != '\'' && *end != ' ' && *end != '>') end++;
						*end = '\0';
						resolve_link(current_url, h, cur_href, sizeof(cur_href));
						in_a = 1;
					}
				} else if (lynx_strcasecmp(tag_buf, "/a") == 0) {
					if (in_a && total_links < MAX_LINKS) {
						char num[16];
						int nlen;
						sprintf(num, "[%d]", total_links + 1);
						nlen = strlen(num);
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

		if (*p == '\n' || *p == '\r' || *p == '\t') {
			if (cur_col > 0 && cur_line[cur_col - 1] != ' ') {
				cur_line[cur_col++] = ' ';
				cur_line[cur_col] = '\0';
			}
			p++;
			continue;
		}

		if (*p == '&') {
			if (strncmp(p, "&lt;", 4) == 0) { cur_line[cur_col++] = '<'; p += 4; }
			else if (strncmp(p, "&gt;", 4) == 0) { cur_line[cur_col++] = '>'; p += 4; }
			else if (strncmp(p, "&amp;", 5) == 0) { cur_line[cur_col++] = '&'; p += 5; }
			else if (strncmp(p, "&quot;", 6) == 0) { cur_line[cur_col++] = '"'; p += 6; }
			else if (strncmp(p, "&nbsp;", 6) == 0) { cur_line[cur_col++] = ' '; p += 6; }
			else if (strncmp(p, "&copy;", 6) == 0) {
				if (cur_col < MAX_LINE_LEN - 4) {
					strcpy(cur_line + cur_col, "(c)");
					cur_col += 3;
				}
				p += 6;
			}
			else { cur_line[cur_col++] = *p++; }
			cur_line[cur_col] = '\0';
		} else {
			cur_line[cur_col++] = *p++;
			cur_line[cur_col] = '\0';
		}

		if (cur_col >= 75 && cur_line[cur_col - 1] == ' ') {
			flush_html_line(cur_line, &cur_col);
		}
	}
	flush_html_line(cur_line, &cur_col);

	if (doc_title[0] == '\0')
		strcpy(doc_title, "SIX Lynx");
}

static void render_source(const char *raw)
{
	const char *p = raw;
	char line_buf[MAX_LINE_LEN];
	int col = 0;

	total_lines = 0;
	total_links = 0;
	strcpy(doc_title, "[Source View]");
	line_buf[0] = '\0';

	while (*p && total_lines < MAX_LINES) {
		if (*p == '\n') {
			line_buf[col] = '\0';
			add_line(line_buf);
			col = 0;
			line_buf[0] = '\0';
		} else if (*p != '\r') {
			if (col < MAX_LINE_LEN - 1) {
				line_buf[col++] = *p;
			}
			if (col >= 78) {
				line_buf[col] = '\0';
				add_line(line_buf);
				col = 0;
				line_buf[0] = '\0';
			}
		}
		p++;
	}
	if (col > 0 && total_lines < MAX_LINES) {
		line_buf[col] = '\0';
		add_line(line_buf);
	}
}

static void render_current(void)
{
	if (!raw_doc) return;
	if (view_source_mode)
		render_source(raw_doc);
	else
		render_html(raw_doc);
}

static void dump_mode(const char *url)
{
	int len;
	char *html = http_fetch(url, &len);
	int i;

	if (!html) {
		printf("lynx: failed to load %s\n", url);
		return;
	}
	strncpy(current_url, url, sizeof(current_url) - 1);
	render_html(html);

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

static void show_help(void)
{
	int rows, cols;
	char ch;
	get_screen_size(&rows, &cols);
	printf("\033[H\033[2J");
	printf("\033[1;36m========================= Lynx Help & Key Bindings =========================\033[0m\r\n\r\n");
	printf("  \033[1mNavigation:\033[0m\r\n");
	printf("    Up / Down or k / j   Move between links or scroll one line\r\n");
	printf("    PgDn / Space / f     Scroll down one page\r\n");
	printf("    PgUp                 Scroll up one page\r\n");
	printf("    b                    Back to previous URL in history\r\n\r\n");
	printf("  \033[1mLinks & URLs:\033[0m\r\n");
	printf("    Enter                Follow selected link\r\n");
	printf("    1 - 9                Jump directly to link number\r\n");
	printf("    g                    Go to URL (supports http://, https://, file://)\r\n");
	printf("    r                    Reload current page\r\n\r\n");
	printf("  \033[1mSearch & Document:\033[0m\r\n");
	printf("    /                    In-page search (case-insensitive)\r\n");
	printf("    n                    Find next occurrence\r\n");
	printf("    \\                    Toggle between rendered HTML and raw source view\r\n");
	printf("    s                    Save page contents to a local file\r\n\r\n");
	printf("  \033[1mBookmarks:\033[0m\r\n");
	printf("    a                    Add current page to bookmarks\r\n");
	printf("    v                    View bookmarks page with clickable links\r\n\r\n");
	printf("  \033[1mGeneral:\033[0m\r\n");
	printf("    h or ?               Show this help screen\r\n");
	printf("    q                    Quit Lynx\r\n\r\n");
	printf("\033[7m  Press any key to return to browsing...  \033[0m");
	fflush(stdout);

	read(0, &ch, 1);
}

static void draw_screen(int top_line, int cur_link, int rows, int cols)
{
	int i;
	int view_rows = rows - 2;

	printf("\033[H");

	/* Top Status Bar */
	printf("\033[7m");
	printf(" Lynx: %-46.46s (%d/%d)", doc_title, top_line + 1, total_lines > 0 ? total_lines : 1);
	for (i = strlen(doc_title) + 20; i < cols; i++) putchar(' ');
	printf("\033[0m\r\n");

	/* Document Body */
	for (i = 0; i < view_rows; i++) {
		int line_idx = top_line + i;
		printf("\033[K");
		if (line_idx < total_lines) {
			if (cur_link >= 0 && cur_link < total_links && links[cur_link].line == line_idx) {
				printf("\033[1;36m%s\033[0m", doc_lines[line_idx]);
			} else {
				printf("%s", doc_lines[line_idx]);
			}
		}
		printf("\r\n");
	}

	/* Bottom Command Bar */
	printf("\033[7m");
	if (status_msg[0] != '\0') {
		printf(" %-70.70s", status_msg);
		status_msg[0] = '\0';
	} else if (cur_link >= 0 && cur_link < total_links) {
		printf(" [%d/%d] %-36.36s Enter:Follow g:URL /:Find \\:Src s:Save a:Bkmk q:Quit",
		       cur_link + 1, total_links, links[cur_link].url);
	} else {
		printf(" Down/Up:Scroll  Tab:Link  Enter:Follow  g:URL  /:Find  \\:Src  s:Save  q:Quit");
	}
	for (i = 75; i < cols; i++) putchar(' ');
	printf("\033[0m");
	fflush(stdout);
}

static void do_search(int *top_line, int *cur_link)
{
	int i, l;
	if (last_search[0] == '\0') return;

	/* Search from top_line + 1 to end */
	for (i = *top_line + 1; i < total_lines; i++) {
		if (lynx_strcasestr(doc_lines[i], last_search)) {
			*top_line = i;
			sprintf(status_msg, "[Found match at line %d]", i + 1);
			for (l = 0; l < total_links; l++) {
				if (links[l].line == i) { *cur_link = l; break; }
			}
			return;
		}
	}

	/* Wrap around from line 0 to top_line */
	for (i = 0; i <= *top_line; i++) {
		if (lynx_strcasestr(doc_lines[i], last_search)) {
			*top_line = i;
			sprintf(status_msg, "[Found match at line %d (wrapped)]", i + 1);
			for (l = 0; l < total_links; l++) {
				if (links[l].line == i) { *cur_link = l; break; }
			}
			return;
		}
	}

	sprintf(status_msg, "[Pattern not found: %s]", last_search);
}

static void get_bookmark_path(char *bpath, size_t sz)
{
	char *home = getenv("HOME");
	if (home && strlen(home) > 0)
		sprintf(bpath, "%s/.lynx_bookmarks", home);
	else
		sprintf(bpath, "/root/.lynx_bookmarks");
}

static void add_bookmark(const char *url, const char *title)
{
	char bpath[256];
	char entry[MAX_URL_LEN + 256];
	int fd;
	get_bookmark_path(bpath, sizeof(bpath));

	fd = open(bpath, O_RDWR | O_CREAT | O_APPEND, 0644);
	if (fd < 0) {
		sprintf(status_msg, "[Failed to open bookmarks file]");
		return;
	}

	sprintf(entry, "<p><a href=\"%s\">%s</a> (%s)</p>\n", url, (title && title[0]) ? title : url, url);
	write(fd, entry, strlen(entry));
	close(fd);

	sprintf(status_msg, "[Bookmark added for %s]", url);
}

static void ensure_bookmarks_exist(void)
{
	char bpath[256];
	int fd;
	get_bookmark_path(bpath, sizeof(bpath));

	fd = open(bpath, O_RDONLY);
	if (fd < 0) {
		fd = open(bpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd >= 0) {
			const char *hdr = "<html><head><title>Lynx Bookmarks</title></head><body><h1>Lynx Bookmarks</h1><hr>\n<p>Press 'a' on any page to bookmark it.</p>\n";
			write(fd, hdr, strlen(hdr));
			close(fd);
		}
	} else {
		close(fd);
	}
}

static void save_page_to_file(const char *filename)
{
	int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	int i;
	if (fd < 0) {
		sprintf(status_msg, "[Cannot open %s for writing]", filename);
		return;
	}

	if (view_source_mode && raw_doc) {
		write(fd, raw_doc, raw_doc_len);
	} else {
		for (i = 0; i < total_lines; i++) {
			write(fd, doc_lines[i], strlen(doc_lines[i]));
			write(fd, "\n", 1);
		}
	}
	close(fd);
	sprintf(status_msg, "[Saved %d lines to %s]", total_lines, filename);
}

static int lynx_getline(char *buf, int max_len)
{
	int pos = 0;
	char ch;

	while (pos < max_len - 1) {
		if (read(0, &ch, 1) <= 0) break;
		if (ch == '\r' || ch == '\n') {
			break;
		} else if (ch == '\b' || ch == 127) {
			if (pos > 0) {
				pos--;
				printf("\b \b");
				fflush(stdout);
			}
		} else if (ch == 3 || ch == 27) {
			buf[0] = '\0';
			return 0;
		} else if (ch >= 32 && ch <= 126) {
			buf[pos++] = ch;
			putchar(ch);
			fflush(stdout);
		}
	}
	buf[pos] = '\0';
	return pos;
}

static void interactive_browse(const char *initial_url)
{
	int top_line = 0;
	int cur_link = -1;
	int rows, cols;
	char url[MAX_URL_LEN];

	strncpy(url, initial_url, sizeof(url) - 1);

load_new_url:
	{
		int len;

		printf("\033[H\033[2JGetting %s...\r\n", url);
		fflush(stdout);

		strncpy(current_url, url, sizeof(current_url) - 1);
		raw_doc = http_fetch(url, &len);
		if (!raw_doc) {
			printf("\r\nFailed to load %s. Press any key to continue.\r\n", url);
			read(0, &rows, 1);
			return;
		}
		raw_doc_len = len;

		view_source_mode = 0;
		render_html(raw_doc);

		top_line = 0;
		cur_link = -1;
	}

	enable_raw_mode();
	printf("\033[?25l");

	for (;;) {
		get_screen_size(&rows, &cols);
		draw_screen(top_line, cur_link, rows, cols);

		char ch;
		if (read(0, &ch, 1) <= 0) break;

		if (ch == 'q' || ch == 'Q') {
			break;
		} else if (ch == '\033') {
			/* Escape sequence: arrow keys */
			char seq[3];
			if (read(0, &seq[0], 1) > 0 && read(0, &seq[1], 1) > 0) {
				if (seq[0] == '[') {
					if (seq[1] == 'A') { /* Up */
						if (cur_link > 0) cur_link--;
						else if (cur_link == 0) cur_link = -1;
						else if (top_line > 0) top_line--;
					} else if (seq[1] == 'B') { /* Down */
						if (cur_link < 0 && total_links > 0) cur_link = 0;
						else if (cur_link < total_links - 1) cur_link++;
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
		} else if (ch == '\t') {
			/* Tab key: cycle through links */
			if (total_links > 0) {
				if (cur_link < 0 || cur_link >= total_links - 1) cur_link = 0;
				else cur_link++;
			}
		} else if (ch == 'j') {
			if (cur_link < 0 && total_links > 0) cur_link = 0;
			else if (cur_link < total_links - 1) cur_link++;
			else if (top_line + (rows - 2) < total_lines) top_line++;
		} else if (ch == 'k') {
			if (cur_link > 0) cur_link--;
			else if (cur_link == 0) cur_link = -1;
			else if (top_line > 0) top_line--;
		} else if (ch == ' ' || ch == 'f') {
			if (top_line + (rows - 2) < total_lines)
				top_line += (rows - 2);
		} else if (ch == 'b' || ch == 'B') {
			if (history_count > 0) {
				history_count--;
				strncpy(url, history_stack[history_count], sizeof(url) - 1);
				goto load_new_url;
			} else {
				top_line -= (rows - 2);
				if (top_line < 0) top_line = 0;
			}
		} else if (ch == '\r' || ch == '\n') {
			if (cur_link >= 0 && cur_link < total_links) {
				if (history_count < MAX_HIST) {
					strncpy(history_stack[history_count++], current_url, MAX_URL_LEN - 1);
				}
				strncpy(url, links[cur_link].url, sizeof(url) - 1);
				goto load_new_url;
			}
		} else if (ch >= '1' && ch <= '9') {
			int lidx = ch - '1';
			if (lidx < total_links) {
				if (history_count < MAX_HIST) {
					strncpy(history_stack[history_count++], current_url, MAX_URL_LEN - 1);
				}
				strncpy(url, links[lidx].url, sizeof(url) - 1);
				goto load_new_url;
			}
		} else if (ch == 'g' || ch == 'G') {
			static char prompt_url[MAX_URL_LEN];
			printf("\033[?25h\033[%d;1H\033[KURL to open: ", rows);
			fflush(stdout);
			if (lynx_getline(prompt_url, sizeof(prompt_url)) > 0) {
				if (history_count < MAX_HIST) {
					strncpy(history_stack[history_count++], current_url, MAX_URL_LEN - 1);
				}
				strncpy(url, prompt_url, sizeof(url) - 1);
				goto load_new_url;
			}
			printf("\033[?25l");
		} else if (ch == 'r' || ch == 'R') {
			goto load_new_url;
		} else if (ch == '/') {
			/* In-page search */
			static char prompt_search[64];
			printf("\033[?25h\033[%d;1H\033[KSearch [/]: ", rows);
			fflush(stdout);
			if (lynx_getline(prompt_search, sizeof(prompt_search)) > 0) {
				strncpy(last_search, prompt_search, sizeof(last_search) - 1);
				do_search(&top_line, &cur_link);
			}
			printf("\033[?25l");
		} else if (ch == 'n' || ch == 'N') {
			/* Next search match */
			if (last_search[0] != '\0') {
				do_search(&top_line, &cur_link);
			} else {
				sprintf(status_msg, "[No previous search pattern]");
			}
		} else if (ch == '\\') {
			/* Toggle source / rendered view */
			view_source_mode = !view_source_mode;
			render_current();
			top_line = 0;
			cur_link = -1;
			sprintf(status_msg, view_source_mode ? "[Source View mode enabled]" : "[Rendered HTML mode enabled]");
		} else if (ch == 's' || ch == 'S') {
			/* Save page to file */
			static char prompt_file[128];
			printf("\033[?25h\033[%d;1H\033[KSave to file: ", rows);
			fflush(stdout);
			if (lynx_getline(prompt_file, sizeof(prompt_file)) > 0) {
				save_page_to_file(prompt_file);
			}
			printf("\033[?25l");
		} else if (ch == 'a' || ch == 'A') {
			/* Bookmark current page */
			add_bookmark(current_url, doc_title);
		} else if (ch == 'v' || ch == 'V') {
			/* View bookmarks */
			char bpath[256];
			char burl[280];
			ensure_bookmarks_exist();
			get_bookmark_path(bpath, sizeof(bpath));
			sprintf(burl, "file://%s", bpath);

			if (history_count < MAX_HIST) {
				strncpy(history_stack[history_count++], current_url, MAX_URL_LEN - 1);
			}
			strncpy(url, burl, sizeof(url) - 1);
			goto load_new_url;
		} else if (ch == 'h' || ch == 'H' || ch == '?') {
			show_help();
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
	printf("\033[?25h\033[H\033[2J");
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
