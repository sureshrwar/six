#include "sh.h"
#include "var.h"
#include "io.h"

#include <linux/times.h>

/*
 * All three are static and defined below their first use; see the note in
 * var.c.  These also draw "static declaration follows non-static
 * declaration", because the implicit declaration GCC invents has external
 * linkage.
 */
_PROTOTYPE(static int brkcontin, (char *cp, int val ));
_PROTOTYPE(static void rdexp, (char **wp, void (*f)(), int key ));
_PROTOTYPE(static void badid, (char *s ));

/*
 * built-in commands: doX
 */

int
dolabel()
{
	return(0);
}

static char prev_dir[256];

int
dochdir(t)
register struct op *t;
{
	register char *cp, *er;
	char oldcwd[256], newcwd[256];
	int is_dash = 0;
	int have_old = 0;

	if (getcwd(oldcwd, sizeof(oldcwd)) != NULL)
		have_old = 1;

	cp = t->words[1];
	if (cp != NULL && strcmp(cp, "-") == 0) {
		struct var *vp = lookup("OLDPWD");
		if (vp != NULL && vp->value != NULL && vp->value[0] != '\0')
			cp = vp->value;
		else if (prev_dir[0] != '\0')
			cp = prev_dir;
		else {
			err("cd: OLDPWD not set");
			return(1);
		}
		is_dash = 1;
	}

	if (cp == NULL && (cp = homedir->value) == NULL)
		er = ": no home directory";
	else if (chdir(cp) < 0)
		er = ": bad directory";
	else {
		if (have_old) {
			strncpy(prev_dir, oldcwd, sizeof(prev_dir) - 1);
			prev_dir[sizeof(prev_dir) - 1] = '\0';
			setval(lookup("OLDPWD"), prev_dir);
		}
		if (getcwd(newcwd, sizeof(newcwd)) != NULL) {
			setval(lookup("PWD"), newcwd);
			if (is_dash) {
				prs(newcwd);
				prs("\n");
			}
		} else if (is_dash) {
			prs(cp);
			prs("\n");
		}
		return(0);
	}
	prs(cp != NULL? cp: "cd");
	err(er);
	return(1);
}

int
doshift(t)
register struct op *t;
{
	register n;

	n = t->words[1]? getn(t->words[1]): 1;
	if(dolc < n) {
		err("nothing to shift");
		return(1);
	}
	dolv[n] = dolv[0];
	dolv += n;
	dolc -= n;
	setval(lookup("#"), putn(dolc));
	return(0);
}

/*
 * execute login and newgrp directly
 */
int
dologin(t)
struct op *t;
{
	register char *cp;

	if (talking) {
		signal(SIGINT, SIG_DFL);
		signal(SIGQUIT, SIG_DFL);
	}
	cp = rexecve(t->words[0], t->words, makenv());
	prs(t->words[0]); prs(": "); err(cp);
	return(1);
}

int
doumask(t)
register struct op *t;
{
	register int i, n;
	register char *cp;

	if ((cp = t->words[1]) == NULL) {
		i = umask(0);
		umask(i);
		for (n=3*4; (n-=3) >= 0;)
			sh_putc('0'+((i>>n)&07));
		sh_putc('\n');
	} else {
		for (n=0; *cp>='0' && *cp<='9'; cp++)
			n = n*8 + (*cp-'0');
		umask(n);
	}
	return(0);
}

int
doexec(t)
register struct op *t;
{
	register i;
	jmp_buf ex;
	xint *ofail;

	t->ioact = NULL;
	for(i = 0; (t->words[i]=t->words[i+1]) != NULL; i++)
		;
	if (i == 0)
		return(1);
	execflg = 1;
	ofail = failpt;
	if (setjmp(failpt = ex) == 0)
		execute(t, NOPIPE, NOPIPE, FEXEC);
	failpt = ofail;
	execflg = 0;
	return(1);
}

int
dodot(t)
struct op *t;
{
	register i;
	register char *sp, *tp;
	char *cp;

	if ((cp = t->words[1]) == NULL)
		return(0);
	sp = any('/', cp)? ":": path->value;
	while (*sp) {
		tp = e.linep;
		while (*sp && (*tp = *sp++) != ':')
			tp++;
		if (tp != e.linep)
			*tp++ = '/';
		for (i = 0; (*tp++ = cp[i++]) != '\0';)
			;
		if ((i = open(e.linep, 0)) >= 0) {
			exstat = 0;
			next(remap(i));
			return(exstat);
		}
	}
	prs(cp);
	err(": not found");
	return(-1);
}

int
dowait(t)
struct op *t;
{
	register i;
	register char *cp;

	if ((cp = t->words[1]) != NULL) {
		i = getn(cp);
		if (i == 0)
			return(0);
	} else
		i = -1;
	setstatus(waitfor(i, 1));
	return(0);
}

int
doread(t)
struct op *t;
{
	register char *cp, **wp;
	register nb;
	register int  nl = 0;

	if (t->words[1] == NULL) {
		err("Usage: read name ...");
		return(1);
	}
	for (wp = t->words+1; *wp; wp++) {
		for (cp = e.linep; !nl && cp < elinep-1; cp++)
			if ((nb = read(0, cp, sizeof(*cp))) != sizeof(*cp) ||
			    (nl = (*cp == '\n')) ||
			    (wp[1] && any(*cp, ifs->value)))
				break;
		*cp = 0;
		if (nb <= 0)
			break;
		setval(lookup(*wp), e.linep);
	}
	return(nb <= 0);
}

int
doeval(t)
register struct op *t;
{
	return(RUN(awordlist, t->words+1, wdchar));
}

int
dotrap(t)
register struct op *t;
{
	register int  n, i;
	register int  resetsig;

	if (t->words[1] == NULL) {
		for (i=0; i<=_NSIG; i++)
			if (trap[i]) {
				prn(i);
				prs(": ");
				prs(trap[i]);
				prs("\n");
			}
		return(0);
	}
	resetsig = digit(*t->words[1]);
	for (i = resetsig ? 1 : 2; t->words[i] != NULL; ++i) {
		n = getsig(t->words[i]);
		xfree(trap[n]);
		trap[n] = 0;
		if (!resetsig) {
			if (*t->words[1] != '\0') {
				trap[n] = strsave(t->words[1], 0);
				setsig(n, sig);
			} else
				setsig(n, SIG_IGN);
		} else {
			if (talking)
				if (n == SIGINT)
					setsig(n, onintr);
				else
					setsig(n, n == SIGQUIT ? SIG_IGN 
							       : SIG_DFL);
			else
				setsig(n, SIG_DFL);
		}
	}
	return(0);
}

int
getsig(s)
char *s;
{
	register int n;

	if ((n = getn(s)) < 0 || n > _NSIG) {
		err("trap: bad signal number");
		n = 0;
	}
	return(n);
}

void
setsig(n, f)
register n;
_PROTOTYPE(void (*f), (int));
{
	if (n == 0)
		return;
	if (signal(n, SIG_IGN) != SIG_IGN || ourtrap[n]) {
		ourtrap[n] = 1;
		signal(n, f);
	}
}

int
getn(as)
char *as;
{
	register char *s;
	register n, m;

	s = as;
	m = 1;
	if (*s == '-') {
		m = -1;
		s++;
	}
	for (n = 0; digit(*s); s++)
		n = (n*10) + (*s-'0');
	if (*s) {
		prs(as);
		err(": bad number");
	}
	return(n*m);
}

int
dobreak(t)
struct op *t;
{
	return(brkcontin(t->words[1], 1));
}

int
docontinue(t)
struct op *t;
{
	return(brkcontin(t->words[1], 0));
}

static int
brkcontin(cp, val)
register char *cp;
int val;
{
	register struct brkcon *bc;
	register nl;

	nl = cp == NULL? 1: getn(cp);
	if (nl <= 0)
		nl = 999;
	do {
		if ((bc = brklist) == NULL)
			break;
		brklist = bc->nextlev;
	} while (--nl);
	if (nl) {
		err("bad break/continue level");
		return(1);
	}
	isbreak = val;
	longjmp(bc->brkpt, 1);
	/* NOTREACHED */
}

int
doexit(t)
struct op *t;
{
	register char *cp;

	execflg = 0;
	if ((cp = t->words[1]) != NULL)
		setstatus(getn(cp));
	leave();
	/* NOTREACHED */
}

int
doexport(t)
struct op *t;
{
	rdexp(t->words+1, export, EXPORT);
	return(0);
}

int
doreadonly(t)
struct op *t;
{
	rdexp(t->words+1, ronly, RONLY);
	return(0);
}

static void
rdexp(wp, f, key)
register char **wp;
void (*f)();
int key;
{
	if (*wp != NULL) {
		for (; *wp != NULL; wp++)
			if (checkname(*wp))
				(*f)(lookup(*wp));
			else
				badid(*wp);
	} else
		putvlist(key, 1);
}

static void
badid(s)
register char *s;
{
	prs(s);
	err(": bad identifier");
}

int
doset(t)
register struct op *t;
{
	register struct var *vp;
	register char *cp;
	register n;

	if ((cp = t->words[1]) == NULL) {
		for (vp = vlist; vp; vp = vp->next)
			varput(vp->name, 1);
		return(0);
	}
	if (strcmp(cp, "-o") == 0 || strcmp(cp, "+o") == 0) {
		extern int sh_vi_mode;
		int enable = (cp[0] == '-');
		char *opt = t->words[2];
		if (opt == NULL) {
			prs("vi\t\t");
			prs(sh_vi_mode ? "on\n" : "off\n");
			prs("emacs\t\t");
			prs(sh_vi_mode ? "off\n" : "on\n");
			return(0);
		}
		if (strcmp(opt, "vi") == 0) {
			sh_vi_mode = enable ? 1 : 0;
			return(0);
		}
		if (strcmp(opt, "emacs") == 0) {
			sh_vi_mode = enable ? 0 : 1;
			return(0);
		}
		err("set: unknown option");
		return(1);
	}
	if (*cp == '-') {
		/* bad: t->words++; */
		for(n = 0; (t->words[n]=t->words[n+1]) != NULL; n++)
			;
		if (*++cp == 0)
			flag['x'] = flag['v'] = 0;
		else
			for (; *cp; cp++)
				switch (*cp) {
				case 'e':
					if (!talking)
						flag['e']++;
					break;

				default:
					if (*cp>='a' && *cp<='z')
						flag[*cp]++;
					break;
				}
		setdash();
	}
	if (t->words[1]) {
		t->words[0] = dolv[0];
		for (n=1; t->words[n]; n++)
			setarea((char *)t->words[n], 0);
		dolc = n-1;
		dolv = t->words;
		setval(lookup("#"), putn(dolc));
		setarea((char *)(dolv-1), 0);
	}
	return(0);
}

void
varput(s, out)
register char *s;
int out;
{
	if (letnum(*s)) {
		write(out, s, strlen(s));
		write(out, "\n", 1);
	}
}


#define	SECS	60L
#define	MINS	3600L

int
dotimes()
{
	struct tms tbuf;

	times(&tbuf);

	prn((int)(tbuf.tms_cutime / MINS));
	prs("m");
	prn((int)((tbuf.tms_cutime % MINS) / SECS));
	prs("s ");
	prn((int)(tbuf.tms_cstime / MINS));
	prs("m");
	prn((int)((tbuf.tms_cstime % MINS) / SECS));
	prs("s\n");
	return(0);
}

void sh_hist_print(void);

int
dohistory()
{
	sh_hist_print();
	return(0);
}

extern int dojobs(struct op *t);
extern int dofg(struct op *t);
extern int dobg(struct op *t);

#include <stat.h>

#define MAX_ALIASES 32
#define ALIAS_NAME_MAX 32
#define ALIAS_VAL_MAX 128

static struct sh_alias {
	char name[ALIAS_NAME_MAX];
	char val[ALIAS_VAL_MAX];
	int  used;
} sh_aliases[MAX_ALIASES];

char *
sh_lookup_alias(name)
const char *name;
{
	int i;
	if (!name || !name[0])
		return NULL;
	for (i = 0; i < MAX_ALIASES; i++) {
		if (sh_aliases[i].used && strcmp(sh_aliases[i].name, name) == 0)
			return sh_aliases[i].val;
	}
	return NULL;
}

static void
sh_set_alias(name, val)
const char *name;
const char *val;
{
	int i, free_idx = -1;
	for (i = 0; i < MAX_ALIASES; i++) {
		if (sh_aliases[i].used && strcmp(sh_aliases[i].name, name) == 0) {
			strncpy(sh_aliases[i].val, val, ALIAS_VAL_MAX - 1);
			sh_aliases[i].val[ALIAS_VAL_MAX - 1] = '\0';
			return;
		}
		if (!sh_aliases[i].used && free_idx < 0)
			free_idx = i;
	}
	if (free_idx >= 0) {
		sh_aliases[free_idx].used = 1;
		strncpy(sh_aliases[free_idx].name, name, ALIAS_NAME_MAX - 1);
		sh_aliases[free_idx].name[ALIAS_NAME_MAX - 1] = '\0';
		strncpy(sh_aliases[free_idx].val, val, ALIAS_VAL_MAX - 1);
		sh_aliases[free_idx].val[ALIAS_VAL_MAX - 1] = '\0';
	}
}

int
doalias(t)
register struct op *t;
{
	int i, w;
	if (t->words[1] == NULL) {
		for (i = 0; i < MAX_ALIASES; i++) {
			if (sh_aliases[i].used) {
				prs("alias ");
				prs(sh_aliases[i].name);
				prs("='");
				prs(sh_aliases[i].val);
				prs("'\n");
			}
		}
		return 0;
	}
	for (w = 1; t->words[w] != NULL; w++) {
		char *arg = t->words[w];
		char *eq = strchr(arg, '=');
		if (eq) {
			char nbuf[ALIAS_NAME_MAX];
			int nlen = (int)(eq - arg);
			if (nlen <= 0)
				continue;
			if (nlen >= ALIAS_NAME_MAX)
				nlen = ALIAS_NAME_MAX - 1;
			memcpy(nbuf, arg, nlen);
			nbuf[nlen] = '\0';
			sh_set_alias(nbuf, eq + 1);
		} else {
			char *val = sh_lookup_alias(arg);
			if (val) {
				prs("alias ");
				prs(arg);
				prs("='");
				prs(val);
				prs("'\n");
			} else {
				prs("alias: ");
				prs(arg);
				prs(": not found\n");
				return 1;
			}
		}
	}
	return 0;
}

int
dounalias(t)
register struct op *t;
{
	int i, w;
	if (t->words[1] == NULL) {
		err("unalias: usage: unalias [-a] name ...");
		return 1;
	}
	if (strcmp(t->words[1], "-a") == 0) {
		for (i = 0; i < MAX_ALIASES; i++)
			sh_aliases[i].used = 0;
		return 0;
	}
	for (w = 1; t->words[w] != NULL; w++) {
		for (i = 0; i < MAX_ALIASES; i++) {
			if (sh_aliases[i].used && strcmp(sh_aliases[i].name, t->words[w]) == 0)
				sh_aliases[i].used = 0;
		}
	}
	return 0;
}

static int
sh_find_in_path(cmd, outpath, outsz)
const char *cmd;
char *outpath;
int outsz;
{
	struct stat st;
	const char *pstr;
	const char *p;

	if (!cmd || !cmd[0])
		return 0;
	if (strchr(cmd, '/')) {
		if (stat((char *)cmd, &st) == 0 && !S_ISDIR(st.st_mode)) {
			strncpy(outpath, cmd, outsz - 1);
			outpath[outsz - 1] = '\0';
			return 1;
		}
		return 0;
	}
	pstr = (path && path->value && path->value[0]) ? path->value : "/bin:/usr/bin:/etc";
	p = pstr;
	while (*p) {
		char dir[128];
		int dlen = 0;
		while (*p && *p != ':' && dlen < (int)sizeof(dir) - 1)
			dir[dlen++] = *p++;
		dir[dlen] = '\0';
		if (*p == ':')
			p++;
		if (dlen == 0)
			strcpy(dir, ".");
		snprintf(outpath, outsz, "%s/%s", dir, cmd);
		if (stat(outpath, &st) == 0 && !S_ISDIR(st.st_mode))
			return 1;
	}
	return 0;
}

int
dotype(t)
register struct op *t;
{
	int w, rc = 0;
	int is_which = (t->words[0] && strcmp(t->words[0], "which") == 0);

	if (t->words[1] == NULL)
		return 1;
	for (w = 1; t->words[w] != NULL; w++) {
		char *arg = t->words[w];
		char *aval = sh_lookup_alias(arg);
		char fullpath[256];
		if (aval) {
			if (is_which) {
				prs("alias "); prs(arg); prs("='"); prs(aval); prs("'\n");
			} else {
				prs(arg); prs(" is aliased to `"); prs(aval); prs("'\n");
			}
		} else if (inbuilt(arg) != NULL) {
			if (is_which) {
				prs(arg); prs(": shell built-in command\n");
			} else {
				prs(arg); prs(" is a shell builtin\n");
			}
		} else if (sh_find_in_path(arg, fullpath, sizeof(fullpath))) {
			if (is_which) {
				prs(fullpath); prs("\n");
			} else {
				prs(arg); prs(" is "); prs(fullpath); prs("\n");
			}
		} else {
			prs(arg); prs(": not found\n");
			rc = 1;
		}
	}
	return rc;
}

int
dotest(t)
register struct op *t;
{
	int argc = 0;
	char **argv;
	int neg = 0;
	struct stat st;

	while (t->words[argc] != NULL)
		argc++;
	if (t->words[0] && strcmp(t->words[0], "[") == 0) {
		if (argc < 2 || strcmp(t->words[argc - 1], "]") != 0) {
			err("[: missing `]'");
			return 2;
		}
		argc--; /* strip trailing ']' */
	}
	argv = &t->words[1];
	argc--;

	while (argc > 0 && strcmp(argv[0], "!") == 0) {
		neg = !neg;
		argv++;
		argc--;
	}

	if (argc <= 0)
		return neg ? 0 : 1;

	if (argc == 1) {
		int res = (argv[0] && argv[0][0] != '\0');
		return (res ^ neg) ? 0 : 1;
	}

	if (argc == 2) {
		char *op = argv[0];
		char *arg = argv[1];
		int res = 0;
		if (strcmp(op, "-z") == 0)
			res = (arg[0] == '\0');
		else if (strcmp(op, "-n") == 0)
			res = (arg[0] != '\0');
		else if (strcmp(op, "-e") == 0 || strcmp(op, "-a") == 0)
			res = (stat(arg, &st) == 0);
		else if (strcmp(op, "-f") == 0)
			res = (stat(arg, &st) == 0 && S_ISREG(st.st_mode));
		else if (strcmp(op, "-d") == 0)
			res = (stat(arg, &st) == 0 && S_ISDIR(st.st_mode));
		else if (strcmp(op, "-s") == 0)
			res = (stat(arg, &st) == 0 && st.st_size > 0);
		else if (strcmp(op, "-L") == 0 || strcmp(op, "-h") == 0)
			res = (lstat(arg, &st) == 0 && S_ISLNK(st.st_mode));
		else if (strcmp(op, "-r") == 0)
			res = (access(arg, 4) == 0);
		else if (strcmp(op, "-w") == 0)
			res = (access(arg, 2) == 0);
		else if (strcmp(op, "-x") == 0)
			res = (access(arg, 1) == 0);
		return (res ^ neg) ? 0 : 1;
	}

	if (argc >= 3) {
		char *s1 = argv[0];
		char *op = argv[1];
		char *s2 = argv[2];
		int res = 0;
		if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0)
			res = (strcmp(s1, s2) == 0);
		else if (strcmp(op, "!=") == 0)
			res = (strcmp(s1, s2) != 0);
		else if (strcmp(op, "-eq") == 0)
			res = (atoi(s1) == atoi(s2));
		else if (strcmp(op, "-ne") == 0)
			res = (atoi(s1) != atoi(s2));
		else if (strcmp(op, "-lt") == 0)
			res = (atoi(s1) < atoi(s2));
		else if (strcmp(op, "-le") == 0)
			res = (atoi(s1) <= atoi(s2));
		else if (strcmp(op, "-gt") == 0)
			res = (atoi(s1) > atoi(s2));
		else if (strcmp(op, "-ge") == 0)
			res = (atoi(s1) >= atoi(s2));
		return (res ^ neg) ? 0 : 1;
	}

	return 1;
}

struct	builtin {
	char	*command;
	int	(*fn)();
};
static struct	builtin	builtin[] = {
	":",		dolabel,
	"cd",		dochdir,
	"shift",	doshift,
	"exec",		doexec,
	"wait",		dowait,
	"read",		doread,
	"eval",		doeval,
	"trap",		dotrap,
	"break",	dobreak,
	"continue",	docontinue,
	"exit",		doexit,
	"export",	doexport,
	"readonly",	doreadonly,
	"set",		doset,
	".",		dodot,
	"source",	dodot,
	"umask",	doumask,
	"login",	dologin,
	"newgrp",	dologin,
	"times",	dotimes,
	"history",	dohistory,
	"jobs",		dojobs,
	"fg",		dofg,
	"bg",		dobg,
	"alias",	doalias,
	"unalias",	dounalias,
	"type",		dotype,
	"which",	dotype,
	"test",		dotest,
	"[",		dotest,
	0,
};

int (*inbuilt(s))()
register char *s;
{
	register struct builtin *bp;

	for (bp = builtin; bp->command != NULL; bp++)
		if (strcmp(bp->command, s) == 0)
			return(bp->fn);
	return((int(*)())NULL);
}

