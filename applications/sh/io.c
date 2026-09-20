#include "sh.h"
#include "io.h"
#include "var.h"

/*
 * shell IO
 */

static struct sh_iobuf sharedbuf = {AFID_NOBUF};
static struct sh_iobuf mainbuf = {AFID_NOBUF};
static unsigned bufid = AFID_ID;	/* buffer id counter */

struct ioarg temparg = {0, 0, 0, AFID_NOBUF, 0};

_PROTOTYPE(static void readhere, (char **name, char *s, int ec ));
_PROTOTYPE(void pushio, (struct ioarg *argp, int (*fn)()));
_PROTOTYPE(static int xxchar, (struct ioarg *ap ));
_PROTOTYPE(void tempname, (char *tname ));

int
sh_getc(ec)
register int ec;
{
	register int c;

	if(e.linep > elinep) {
		while((c=readc()) != '\n' && c)
			;
		err("input line too long");
		gflg++;
		return(c);
	}
	c = readc();
 	if (ec != '\'' && e.iop->task != XGRAVE) {
		if(c == '\\') {
			c = readc();
			if (c == '\n' && ec != '\"')
				return(sh_getc(ec));
			c |= QUOTE;
		}
	}
	return(c);
}

void
unget(c)
int c;
{
	if (e.iop >= e.iobase)
		e.iop->peekc = c;
}

int
eofc()

{
  return e.iop < e.iobase || (e.iop->peekc == 0 && e.iop->prev == 0);
}

int
readc()
{
	register c;

	for (; e.iop >= e.iobase; e.iop--)
		if ((c = e.iop->peekc) != '\0') {
			e.iop->peekc = 0;
			return(c);
		}
		else {
		    if (e.iop->prev != 0) {
		        if ((c = (*e.iop->iofn)(e.iop->argp, e.iop)) != '\0') {
			        if (c == -1) {
				        e.iop++;
				        continue;
			        }
			        if (e.iop == iostack)
				        ioecho(c);
			        return(e.iop->prev = c);
		        }
		        else if (e.iop->task == XIO && e.iop->prev != '\n') {
			        e.iop->prev = 0;
				if (e.iop == iostack)
					ioecho('\n');
			        return '\n';
		        }
		    }
		    if (e.iop->task == XIO) {
			if (multiline)
			    return e.iop->prev = 0;
			if (talking && e.iop == iostack+1)
			    prs(prompt->value);
		    }
		}
	if (e.iop >= iostack)
		return(0);
	leave();
	/* NOTREACHED */
}

void
ioecho(c)
char c;
{
	if (flag['v'])
		write(2, &c, sizeof c);
}

void
pushio(argp, fn)
struct ioarg *argp;
int (*fn)();
{
	if (++e.iop >= &iostack[NPUSH]) {
		e.iop--;
		err("Shell input nested too deeply");
		gflg++;
		return;
	}
	e.iop->iofn = fn;

	if (argp->afid != AFID_NOBUF)
	  e.iop->argp = argp;
	else {
	  e.iop->argp  = ioargstack + (e.iop - iostack);
	  *e.iop->argp = *argp;
	  e.iop->argp->afbuf = e.iop == &iostack[0] ? &mainbuf : &sharedbuf;
	  if (isatty(e.iop->argp->afile) == 0 &&
	      (e.iop == &iostack[0] ||
	       lseek(e.iop->argp->afile, 0L, 1) != -1)) {
	    if (++bufid == AFID_NOBUF)
	      bufid = AFID_ID;
	    e.iop->argp->afid  = bufid;
	  }
	}

	e.iop->prev  = ~'\n';
	e.iop->peekc = 0;
	e.iop->xchar = 0;
	e.iop->nlcount = 0;
	if (fn == filechar || fn == linechar)
		e.iop->task = XIO;
	else if (fn == gravechar || fn == qgravechar)
		e.iop->task = XGRAVE;
	else
		e.iop->task = XOTHER;
}

struct io *
setbase(ip)
struct io *ip;
{
	register struct io *xp;

	xp = e.iobase;
	e.iobase = ip;
	return(xp);
}

/*
 * Input generating functions
 */

/*
 * Produce the characters of a string, then a newline, then EOF.
 */
int
nlchar(ap)
register struct ioarg *ap;
{
	register int c;

	if (ap->aword == NULL)
		return(0);
	if ((c = *ap->aword++) == 0) {
		ap->aword = NULL;
		return('\n');
	}
	return(c);
}

/*
 * Given a list of words, produce the characters
 * in them, with a space after each word.
 */
int
wdchar(ap)
register struct ioarg *ap;
{
	register char c;
	register char **wl;

	if ((wl = ap->awordlist) == NULL)
		return(0);
	if (*wl != NULL) {
		if ((c = *(*wl)++) != 0)
			return(c & 0177);
		ap->awordlist++;
		return(' ');
	}
	ap->awordlist = NULL;
	return('\n');
}

/*
 * Return the characters of a list of words,
 * producing a space between them.
 */
int
dolchar(ap)
register struct ioarg *ap;
{
	register char *wp;

	if ((wp = *ap->awordlist++) != NULL) {
		PUSHIO(aword, wp, *ap->awordlist == NULL? strchar: xxchar);
		return(-1);
	}
	return(0);
}

static int
xxchar(ap)
register struct ioarg *ap;
{
	register int c;

	if (ap->aword == NULL)
		return(0);
	if ((c = *ap->aword++) == '\0') {
		ap->aword = NULL;
		return(' ');
	}
	return(c);
}

/*
 * Produce the characters from a single word (string).
 */
int
strchar(ap)
register struct ioarg *ap;
{
	register int c;

	if (ap->aword == NULL || (c = *ap->aword++) == 0)
		return(0);
	return(c);
}

/*
 * Produce quoted characters from a single word (string).
 */
int
qstrchar(ap)
register struct ioarg *ap;
{
	register int c;

	if (ap->aword == NULL || (c = *ap->aword++) == 0)
		return(0);
	return(c|QUOTE);
}

#include <linux/termios.h>

#define HIST_MAX	64
#define HIST_LINE_MAX	256

static char hist_lines[HIST_MAX][HIST_LINE_MAX];
static int  hist_count = 0;
static int  hist_loaded = 0;

static char rl_linebuf[HIST_LINE_MAX];
static int  rl_linepos = 0;
static int  rl_linelen = 0;

static struct termios rl_saved_tio;
static int            rl_tio_active = 0;

static void
sh_hist_path(buf, sz)
char *buf;
int sz;
{
	char *h = (homedir && homedir->value && homedir->value[0]) ? homedir->value : "/";
	if (strcmp(h, "/") == 0) {
		strncpy(buf, "/.bash_history", sz - 1);
	} else {
		strncpy(buf, h, sz - 16);
		buf[sz - 16] = '\0';
		strcat(buf, "/.bash_history");
	}
	buf[sz - 1] = '\0';
}

static void
sh_hist_mem_push(line)
char *line;
{
	int i;
	if (!line || !line[0])
		return;
	if (hist_count > 0 && strcmp(hist_lines[hist_count - 1], line) == 0)
		return;
	if (hist_count < HIST_MAX) {
		strncpy(hist_lines[hist_count], line, HIST_LINE_MAX - 1);
		hist_lines[hist_count][HIST_LINE_MAX - 1] = '\0';
		hist_count++;
	} else {
		for (i = 1; i < HIST_MAX; i++)
			strcpy(hist_lines[i - 1], hist_lines[i]);
		strncpy(hist_lines[HIST_MAX - 1], line, HIST_LINE_MAX - 1);
		hist_lines[HIST_MAX - 1][HIST_LINE_MAX - 1] = '\0';
	}
}

static void
sh_hist_load()
{
	char path[128];
	char fbuf[512];
	char lbuf[HIST_LINE_MAX];
	int fd, n, i, lpos;

	if (hist_loaded)
		return;
	hist_loaded = 1;
	sh_hist_path(path, sizeof(path));
	fd = open(path, 0);
	if (fd < 0)
		return;
	lpos = 0;
	while ((n = read(fd, fbuf, sizeof(fbuf))) > 0) {
		for (i = 0; i < n; i++) {
			if (fbuf[i] == '\n' || fbuf[i] == '\r') {
				lbuf[lpos] = '\0';
				if (lpos > 0)
					sh_hist_mem_push(lbuf);
				lpos = 0;
			} else if (lpos < HIST_LINE_MAX - 1) {
				lbuf[lpos++] = fbuf[i];
			}
		}
	}
	if (lpos > 0) {
		lbuf[lpos] = '\0';
		sh_hist_mem_push(lbuf);
	}
	close(fd);
}

static void
sh_hist_add(line)
char *line;
{
	char path[128];
	int fd, len;

	if (!line || !line[0])
		return;
	sh_hist_load();
	sh_hist_mem_push(line);
	sh_hist_path(path, sizeof(path));
	fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
	if (fd >= 0) {
		len = strlen(line);
		write(fd, line, len);
		write(fd, "\n", 1);
		close(fd);
	}
}

void
sh_restore_tty()
{
	rl_linepos = 0;
	rl_linelen = 0;
	if (rl_tio_active) {
		tcsetattr(0, &rl_saved_tio);
		rl_tio_active = 0;
	}
}

void
sh_hist_print()
{
	int i;
	sh_hist_load();
	for (i = 0; i < hist_count; i++) {
		prs("  ");
		prn(i + 1);
		prs("  ");
		prs(hist_lines[i]);
		prs("\n");
	}
}

static void
rl_replace_line(buf, lenp, newstr)
char *buf;
int *lenp;
char *newstr;
{
	int cur = *lenp;
	int nlen = strlen(newstr);
	if (nlen > HIST_LINE_MAX - 2)
		nlen = HIST_LINE_MAX - 2;
	while (cur > 0) {
		write(1, "\b \b", 3);
		cur--;
	}
	memcpy(buf, newstr, nlen);
	buf[nlen] = '\0';
	if (nlen > 0)
		write(1, buf, nlen);
	*lenp = nlen;
}

#include <linux/dirent.h>
#include <stat.h>

#define TAB_MAX_MATCHES 32

static char tab_matches[TAB_MAX_MATCHES][32];
static int  tab_is_dir[TAB_MAX_MATCHES];
static char tab_word[128];
static char tab_dir[128];
static char tab_prefix[32];
static char tab_dbuf[512];
static char tab_full_check[256];

static void
sh_tab_complete(outbuf, lenp)
char *outbuf;
int *lenp;
{
	int wstart = *lenp;
	int match_count = 0;
	int is_command = 0;
	int wlen, plen;
	char *slash;
	int fd, n;
	int m_idx, c_idx, common_len;

	while (wstart > 0 && outbuf[wstart - 1] != ' ' && outbuf[wstart - 1] != '\t' &&
	       outbuf[wstart - 1] != '|' && outbuf[wstart - 1] != ';' &&
	       outbuf[wstart - 1] != '&')
		wstart--;

	wlen = *lenp - wstart;
	if (wlen <= 0 || wlen >= (int)sizeof(tab_word)) {
		write(1, "\a", 1);
		return;
	}
	memcpy(tab_word, &outbuf[wstart], wlen);
	tab_word[wlen] = '\0';

	slash = strrchr(tab_word, '/');
	if (slash) {
		int dir_len = (int)(slash - tab_word);
		if (dir_len == 0) {
			strcpy(tab_dir, "/");
		} else {
			memcpy(tab_dir, tab_word, dir_len);
			tab_dir[dir_len] = '\0';
		}
		strncpy(tab_prefix, slash + 1, sizeof(tab_prefix) - 1);
		tab_prefix[sizeof(tab_prefix) - 1] = '\0';
	} else {
		if (wstart == 0) {
			is_command = 1;
			strcpy(tab_dir, "/bin");
			strncpy(tab_prefix, tab_word, sizeof(tab_prefix) - 1);
			tab_prefix[sizeof(tab_prefix) - 1] = '\0';
		} else {
			strcpy(tab_dir, ".");
			strncpy(tab_prefix, tab_word, sizeof(tab_prefix) - 1);
			tab_prefix[sizeof(tab_prefix) - 1] = '\0';
		}
	}

	plen = strlen(tab_prefix);
	fd = open(tab_dir, 0);
	if (fd < 0) {
		write(1, "\a", 1);
		return;
	}

	while ((n = getdents(fd, (struct dirent *)tab_dbuf, sizeof(tab_dbuf))) > 0) {
		int cur = 0;
		while (cur < n) {
			struct dirent *de = (struct dirent *)(tab_dbuf + cur);
			cur += de->d_reclen;
			if (de->d_ino == 0) continue;
			if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
				continue;
			if (plen == 0 || strncmp(de->d_name, tab_prefix, plen) == 0) {
				if (match_count < TAB_MAX_MATCHES) {
					struct stat st;
					strncpy(tab_matches[match_count], de->d_name, 31);
					tab_matches[match_count][31] = '\0';
					if (strcmp(tab_dir, "/") == 0)
						sprintf(tab_full_check, "/%s", de->d_name);
					else
						sprintf(tab_full_check, "%s/%s", tab_dir, de->d_name);
					tab_is_dir[match_count] = (stat(tab_full_check, &st) == 0 && S_ISDIR(st.st_mode));
					match_count++;
				}
			}
		}
	}
	close(fd);

	if (match_count == 0 && is_command) {
		strcpy(tab_dir, ".");
		fd = open(tab_dir, 0);
		if (fd >= 0) {
			while ((n = getdents(fd, (struct dirent *)tab_dbuf, sizeof(tab_dbuf))) > 0) {
				int cur = 0;
				while (cur < n) {
					struct dirent *de = (struct dirent *)(tab_dbuf + cur);
					cur += de->d_reclen;
					if (de->d_ino == 0) continue;
					if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
						continue;
					if (plen == 0 || strncmp(de->d_name, tab_prefix, plen) == 0) {
						if (match_count < TAB_MAX_MATCHES) {
							struct stat st;
							strncpy(tab_matches[match_count], de->d_name, 31);
							tab_matches[match_count][31] = '\0';
							tab_is_dir[match_count] = (stat(de->d_name, &st) == 0 && S_ISDIR(st.st_mode));
							match_count++;
						}
					}
				}
			}
			close(fd);
		}
	}

	if (match_count == 0) {
		write(1, "\a", 1);
		return;
	}

	if (match_count == 1) {
		const char *m = tab_matches[0];
		int mlen = strlen(m);
		char suffix = tab_is_dir[0] ? '/' : ' ';
		if (mlen > plen) {
			const char *append = m + plen;
			int app_len = mlen - plen;
			if (*lenp + app_len + 1 < HIST_LINE_MAX - 2) {
				memcpy(&outbuf[*lenp], append, app_len);
				*lenp += app_len;
				write(1, append, app_len);
			}
		}
		if (*lenp < HIST_LINE_MAX - 2) {
			outbuf[(*lenp)++] = suffix;
			outbuf[*lenp] = '\0';
			write(1, &suffix, 1);
		}
		return;
	}

	common_len = strlen(tab_matches[0]);
	for (m_idx = 1; m_idx < match_count; m_idx++) {
		for (c_idx = 0; c_idx < common_len; c_idx++) {
			if (tab_matches[m_idx][c_idx] != tab_matches[0][c_idx]) {
				common_len = c_idx;
				break;
			}
		}
	}

	if (common_len > plen) {
		const char *append = tab_matches[0] + plen;
		int app_len = common_len - plen;
		if (*lenp + app_len < HIST_LINE_MAX - 2) {
			memcpy(&outbuf[*lenp], append, app_len);
			*lenp += app_len;
			outbuf[*lenp] = '\0';
			write(1, append, app_len);
		}
		return;
	}

	write(1, "\n", 1);
	for (m_idx = 0; m_idx < match_count; m_idx++) {
		write(1, tab_matches[m_idx], strlen(tab_matches[m_idx]));
		if (tab_is_dir[m_idx])
			write(1, "/", 1);
		write(1, "  ", 2);
	}
	write(1, "\n", 1);

	prs(prompt->value ? prompt->value : "bash# ");
	write(1, outbuf, *lenp);
}

static int
sh_readline(outbuf)
char *outbuf;
{
	struct termios raw_tio;
	char saved_cur[HIST_LINE_MAX];
	int len = 0;
	int hist_idx;
	int r;
	unsigned char ch, s1, s2;

	sh_hist_load();
	saved_cur[0] = '\0';
	hist_idx = hist_count;

	if (tcgetattr(0, &rl_saved_tio) == 0) {
		raw_tio = rl_saved_tio;
		raw_tio.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHOK | ECHONL | ECHOCTL);
		raw_tio.c_cc[VMIN] = 1;
		raw_tio.c_cc[VTIME] = 0;
		tcsetattr(0, &raw_tio);
		rl_tio_active = 1;
	}

	for (;;) {
		r = read(0, &ch, 1);
		if (r <= 0) {
			if (r < 0 && errno == EINTR)
				continue;
			sh_restore_tty();
			return 0;
		}
		if (ch == '\t') {
			sh_tab_complete(outbuf, &len);
			continue;
		}
		if (ch == '\r' || ch == '\n') {
			write(1, "\n", 1);
			outbuf[len] = '\0';
			sh_hist_add(outbuf);
			outbuf[len++] = '\n';
			outbuf[len] = '\0';
			sh_restore_tty();
			return len;
		}
		if (ch == 0x04) { /* Ctrl+D */
			if (len == 0) {
				write(1, "\n", 1);
				sh_restore_tty();
				return 0;
			}
			continue;
		}
		if (ch == '\b' || ch == 0x7f) { /* Backspace / DEL */
			if (len > 0) {
				len--;
				outbuf[len] = '\0';
				write(1, "\b \b", 3);
			}
			continue;
		}
		if (ch == 0x15) { /* Ctrl+U: kill line */
			while (len > 0) {
				len--;
				write(1, "\b \b", 3);
			}
			outbuf[0] = '\0';
			continue;
		}
		if (ch == 0x17) { /* Ctrl+W: erase word */
			while (len > 0 && outbuf[len - 1] == ' ') {
				len--;
				write(1, "\b \b", 3);
			}
			while (len > 0 && outbuf[len - 1] != ' ') {
				len--;
				write(1, "\b \b", 3);
			}
			outbuf[len] = '\0';
			continue;
		}
		if (ch == 0x1b) { /* ESC sequence (arrow keys: ESC [ A / B) */
			if (read(0, &s1, 1) <= 0)
				continue;
			if (s1 == '[' || s1 == 'O') {
				if (read(0, &s2, 1) <= 0)
					continue;
				if (s2 == 'A') { /* Up arrow */
					if (hist_idx > 0) {
						if (hist_idx == hist_count) {
							outbuf[len] = '\0';
							strcpy(saved_cur, outbuf);
						}
						hist_idx--;
						rl_replace_line(outbuf, &len, hist_lines[hist_idx]);
					}
				} else if (s2 == 'B') { /* Down arrow */
					if (hist_idx < hist_count) {
						hist_idx++;
						if (hist_idx == hist_count)
							rl_replace_line(outbuf, &len, saved_cur);
						else
							rl_replace_line(outbuf, &len, hist_lines[hist_idx]);
					}
				}
			}
			continue;
		}
		if (ch >= ' ' && ch < 0x7f) {
			if (len < HIST_LINE_MAX - 2) {
				outbuf[len++] = ch;
				outbuf[len] = '\0';
				write(1, &ch, 1);
			}
		}
	}
}

/*
 * Return the characters from a file.
 */
int
filechar(ap)
register struct ioarg *ap;
{
	register int i;
	char c;
	struct sh_iobuf *bp = ap->afbuf;

	if (ap->afid != AFID_NOBUF) {
	  if ((i = ap->afid != bp->id) || bp->bufp == bp->ebufp) {
	    if (i)
	      lseek(ap->afile, ap->afpos, 0);
	    do {
	      i = read(ap->afile, bp->buf, sizeof(bp->buf));
	    } while (i < 0 && errno == EINTR);
	    if (i <= 0) {
	      closef(ap->afile);
	      return 0;
	    }
	    bp->id = ap->afid;
	    bp->ebufp = (bp->bufp  = bp->buf) + i;
	  }
	  ap->afpos++;
	  return *bp->bufp++ & 0177;
	}

	if (ap->afile == 0 && talking && e.iop == iostack) {
		if (rl_linepos >= rl_linelen) {
			rl_linelen = sh_readline(rl_linebuf);
			rl_linepos = 0;
			if (rl_linelen <= 0) {
				closef(ap->afile);
				return 0;
			}
		}
		return rl_linebuf[rl_linepos++] & 0177;
	}

	do {
		i = read(ap->afile, &c, sizeof(c));
	} while (i < 0 && errno == EINTR);
	return(i == sizeof(c)? c&0177: (closef(ap->afile), 0));
}

/*
 * Return the characters from a here temp file.
 */
int
herechar(ap)
register struct ioarg *ap;
{
	char c;


	if (read(ap->afile, &c, sizeof(c)) != sizeof(c)) {
		close(ap->afile);
		c = 0;
	}
	return (c);

}

/*
 * Return the characters produced by a process (`...`).
 * Quote them if required, and remove any trailing newline characters.
 */
int
gravechar(ap, iop)
struct ioarg *ap;
struct io *iop;
{
	register int c;

	if ((c = qgravechar(ap, iop)&~QUOTE) == '\n')
		c = ' ';
	return(c);
}

int
qgravechar(ap, iop)
register struct ioarg *ap;
struct io *iop;
{
	register int c;

	if (iop->xchar) {
		if (iop->nlcount) {
			iop->nlcount--;
			return('\n'|QUOTE);
		}
		c = iop->xchar;
		iop->xchar = 0;
	} else if ((c = filechar(ap)) == '\n') {
		iop->nlcount = 1;
		while ((c = filechar(ap)) == '\n')
			iop->nlcount++;
		iop->xchar = c;
		if (c == 0)
			return(c);
		iop->nlcount--;
		c = '\n';
	}
	return(c!=0? c|QUOTE: 0);
}

/*
 * Return a single command (usually the first line) from a file.
 */
int
linechar(ap)
register struct ioarg *ap;
{
	register int c;

	if ((c = filechar(ap)) == '\n') {
		if (!multiline) {
			closef(ap->afile);
			ap->afile = -1;	/* illegal value */
		}
	}
	return(c);
}

void
prs(s)
register char *s;
{
	if (*s)
		write(2, s, strlen(s));
}

void
sh_putc(c)
char c;
{
	write(2, &c, sizeof c);
}

void
prn(u)
unsigned u;
{
	prs(itoa(u, 0));
}

void
closef(i)
register int i;
{
	if (i > 2)
		close(i);
}

void
closeall()
{
	register u;

	for (u=NUFILE; u<NOFILE;)
		close(u++);
}

/*
 * remap fd into Shell's fd space
 */
int
remap(fd)
register int fd;
{
	register int i;
	int map[NOFILE];

	if (fd < e.iofd) {
		for (i=0; i<NOFILE; i++)
			map[i] = 0;
		do {
			map[fd] = 1;
			fd = dup(fd);
		} while (fd >= 0 && fd < e.iofd);
		for (i=0; i<NOFILE; i++)
			if (map[i])
				close(i);
		if (fd < 0)
			err("too many files open in shell");
	}
	return(fd);
}

int
openpipe(pv)
register int *pv;
{
	register int i;

	if ((i = pipe(pv)) < 0)
		err("can't create pipe - try again");
	return(i);
}

void
closepipe(pv)
register int *pv;
{
	if (pv != NULL) {
		close(*pv++);
		close(*pv);
	}
}

