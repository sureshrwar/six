#include "sh.h"
#include "area.h"
#include "var.h"

/*
 * execute tree
 */

static	char	*signame[] = {
	"Signal 0",
	"Hangup",
	(char *)NULL,	/* interrupt */
	"Quit",
	"Illegal instruction",
	"Trace/BPT trap",
	"Abort",
	"EMT trap",
	"Floating exception",
	"Killed",
	"Bus error",
	"Memory fault",
	"Bad system call",
	(char *)NULL,	/* broken pipe */
	"Alarm clock",
	"Terminated",
};
#define	NSIGNAL (sizeof(signame)/sizeof(signame[0]))


_PROTOTYPE(static int forkexec, (struct op *t, int *pin, int *pout, int act, char **wp, int *pforked ));
_PROTOTYPE(static int parent, (void));
_PROTOTYPE(int iosetup, (struct ioword *iop, int pipein, int pipeout ));
_PROTOTYPE(static void echo, (char **wp ));
_PROTOTYPE(static struct op **find1case, (struct op *t, char *w ));
_PROTOTYPE(static struct op *findcase, (struct op *t, char *w ));
_PROTOTYPE(static void brkset, (struct brkcon *bc ));
_PROTOTYPE(int dolabel, (void));
_PROTOTYPE(int dochdir, (struct op *t ));
_PROTOTYPE(int doshift, (struct op *t ));
_PROTOTYPE(int dologin, (struct op *t ));
_PROTOTYPE(int doumask, (struct op *t ));
_PROTOTYPE(int doexec, (struct op *t ));
_PROTOTYPE(int dodot, (struct op *t ));
_PROTOTYPE(int dowait, (struct op *t ));
_PROTOTYPE(int doread, (struct op *t ));
_PROTOTYPE(int doeval, (struct op *t ));
_PROTOTYPE(int dotrap, (struct op *t ));
_PROTOTYPE(int getsig, (char *s ));
_PROTOTYPE(void setsig, (int n, void (*f)()));
_PROTOTYPE(int getn, (char *as ));
_PROTOTYPE(int dobreak, (struct op *t ));
_PROTOTYPE(int docontinue, (struct op *t ));
_PROTOTYPE(static int brkcontin, (char *cp, int val ));
_PROTOTYPE(int doexit, (struct op *t ));
_PROTOTYPE(int doexport, (struct op *t ));
_PROTOTYPE(int doreadonly, (struct op *t ));
_PROTOTYPE(static void rdexp, (char **wp, void (*f)(), int key));
_PROTOTYPE(static void badid, (char *s ));
_PROTOTYPE(int doset, (struct op *t ));
_PROTOTYPE(void varput, (char *s, int out ));
_PROTOTYPE(int dotimes, (void));

#define MAX_JOBS 16
#define JOB_NONE    0
#define JOB_RUNNING 1
#define JOB_STOPPED 2

struct sh_job {
	int jid;
	int pid;
	int state;
	char cmd[64];
};

static struct sh_job sh_jobs[MAX_JOBS];
static char sh_last_cmd[64];
extern void sh_restore_tty(void);

static void sh_save_cmd(struct op *t)
{
	int i;
	sh_last_cmd[0] = '\0';
	if (!t || !t->words || !t->words[0])
		return;
	for (i = 0; t->words[i]; i++) {
		if (i > 0 && strlen(sh_last_cmd) + 2 < sizeof(sh_last_cmd))
			strcat(sh_last_cmd, " ");
		if (strlen(sh_last_cmd) + strlen(t->words[i]) + 1 < sizeof(sh_last_cmd))
			strcat(sh_last_cmd, t->words[i]);
	}
}

static int sh_add_job(int pid, int state, const char *cmd)
{
	int i, max_jid = 0;
	for (i = 0; i < MAX_JOBS; i++) {
		if (sh_jobs[i].state != JOB_NONE && sh_jobs[i].pid == pid) {
			sh_jobs[i].state = state;
			if (cmd && cmd[0]) {
				strncpy(sh_jobs[i].cmd, cmd, sizeof(sh_jobs[i].cmd) - 1);
				sh_jobs[i].cmd[sizeof(sh_jobs[i].cmd) - 1] = '\0';
			}
			return sh_jobs[i].jid;
		}
		if (sh_jobs[i].state != JOB_NONE && sh_jobs[i].jid > max_jid)
			max_jid = sh_jobs[i].jid;
	}
	for (i = 0; i < MAX_JOBS; i++) {
		if (sh_jobs[i].state == JOB_NONE) {
			sh_jobs[i].jid = max_jid + 1;
			sh_jobs[i].pid = pid;
			sh_jobs[i].state = state;
			strncpy(sh_jobs[i].cmd, (cmd && cmd[0]) ? cmd : "job", sizeof(sh_jobs[i].cmd) - 1);
			sh_jobs[i].cmd[sizeof(sh_jobs[i].cmd) - 1] = '\0';
			return sh_jobs[i].jid;
		}
	}
	return 1;
}

static void sh_remove_job(int pid)
{
	int i;
	for (i = 0; i < MAX_JOBS; i++) {
		if (sh_jobs[i].state != JOB_NONE && sh_jobs[i].pid == pid)
			sh_jobs[i].state = JOB_NONE;
	}
}

int
execute(t, pin, pout, act)
register struct op *t;
int *pin, *pout;
int act;
{
	register struct op *t1;
	int i, pv[2], rv, child, a;
	char *cp, **wp, **wp2;
	struct var *vp;
	struct brkcon bc;

	if (t == NULL)
		return(0);
	rv = 0;
	a = areanum++;
	wp = (wp2 = t->words) != NULL
	     ? eval(wp2, t->type == TCOM ? DOALL : DOALL & ~DOKEY)
	     : NULL;

	switch(t->type) {
	case TPAREN:
	case TCOM:
		rv = forkexec(t, pin, pout, act, wp, &child);
		if (child) {
			exstat = rv;
			leave();
		}
		break;

	case TPIPE:
		if ((rv = openpipe(pv)) < 0)
			break;
		pv[0] = remap(pv[0]);
		pv[1] = remap(pv[1]);
		(void) execute(t->left, pin, pv, 0);
		rv = execute(t->right, pv, pout, 0);
		break;

	case TLIST:
		(void) execute(t->left, pin, pout, 0);
		rv = execute(t->right, pin, pout, 0);
		break;

	case TASYNC:
		if (t->left)
			sh_save_cmd(t->left);
		i = parent();
		if (i != 0) {
			if (i != -1) {
				setval(lookup("!"), putn(i));
				if (pin != NULL)
					closepipe(pin);
				if (talking) {
					int jid = sh_add_job(i, JOB_RUNNING, sh_last_cmd);
					prs("["); prn(jid); prs("] ");
					prs(putn(i));
					prs("\n");
				}
			} else
				rv = -1;
			setstatus(rv);
		} else {
			signal(SIGINT, SIG_IGN);
			signal(SIGQUIT, SIG_IGN);
			signal(SIGTSTP, SIG_IGN);
			if (talking)
				signal(SIGTERM, SIG_DFL);
			talking = 0;
			if (pin == NULL) {
				close(0);
				open("/dev/null", 0);
			}
			exit(execute(t->left, pin, pout, FEXEC));
		}
		break;

	case TOR:
	case TAND:
		rv = execute(t->left, pin, pout, 0);
		if ((t1 = t->right)!=NULL && (rv == 0) == (t->type == TAND))
			rv = execute(t1, pin, pout, 0);
		break;

	case TFOR:
		if (wp == NULL) {
			wp = dolv+1;
			if ((i = dolc) < 0)
				i = 0;
		} else {
			i = -1;
			while (*wp++ != NULL)
				;			
		}
		vp = lookup(t->str);
		while (setjmp(bc.brkpt))
			if (isbreak)
				goto broken;
		brkset(&bc);
		for (t1 = t->left; i-- && *wp != NULL;) {
			setval(vp, *wp++);
			rv = execute(t1, pin, pout, 0);
		}
		brklist = brklist->nextlev;
		break;

	case TWHILE:
	case TUNTIL:
		while (setjmp(bc.brkpt))
			if (isbreak)
				goto broken;
		brkset(&bc);
		t1 = t->left;
		while ((execute(t1, pin, pout, 0) == 0) == (t->type == TWHILE))
			rv = execute(t->right, pin, pout, 0);
		brklist = brklist->nextlev;
		break;

	case TIF:
	case TELIF:
	 	if (t->right != NULL) {
		rv = !execute(t->left, pin, pout, 0) ?
			execute(t->right->left, pin, pout, 0):
			execute(t->right->right, pin, pout, 0);
		}
		break;

	case TCASE:
		if ((cp = evalstr(t->str, DOSUB|DOTRIM)) == 0)
			cp = "";
		if ((t1 = findcase(t->left, cp)) != NULL)
			rv = execute(t1, pin, pout, 0);
		break;

	case TBRACE:
/*
		if (iopp = t->ioact)
			while (*iopp)
				if (iosetup(*iopp++, pin!=NULL, pout!=NULL)) {
					rv = -1;
					break;
				}
*/
		if (rv >= 0 && (t1 = t->left))
			rv = execute(t1, pin, pout, 0);
		break;
	}

broken:
	t->words = wp2;
	isbreak = 0;
	freehere(areanum);
	freearea(areanum);
	areanum = a;
	if (talking && intr) {
		closeall();
		fail();
	}
	if ((i = trapset) != 0) {
		trapset = 0;
		runtrap(i);
	}
	return(rv);
}

static int
forkexec(t, pin, pout, act, wp, pforked)
register struct op *t;
int *pin, *pout;
int act;
char **wp;
int *pforked;
{
	int i, rv, (*shcom)();
	register int f;
	char *cp;
	struct ioword **iopp;
	int resetsig;
	char **owp;

	owp = wp;
	resetsig = 0;
	*pforked = 0;
	shcom = NULL;
	rv = -1;	/* system-detected error */
	if (t->type == TCOM) {
		extern char *sh_lookup_alias(const char *name);
		while ((cp = *wp++) != NULL)
			;
		cp = *wp;

		if (cp != NULL) {
			char *aval = sh_lookup_alias(cp);
			if (aval != NULL) {
				static char abuf[128];
				char *atoks[16];
				int natoks = 0, orig_cnt = 0, k;
				char *p = abuf;
				char **nwp;

				strncpy(abuf, aval, sizeof(abuf) - 1);
				abuf[sizeof(abuf) - 1] = '\0';
				while (*p && natoks < 15) {
					while (*p == ' ' || *p == '\t')
						p++;
					if (!*p)
						break;
					atoks[natoks++] = p;
					while (*p && *p != ' ' && *p != '\t')
						p++;
					if (*p)
						*p++ = '\0';
				}
				if (natoks > 0) {
					while (wp[orig_cnt] != NULL)
						orig_cnt++;
					nwp = (char **)space((natoks + orig_cnt + 2) * sizeof(char *));
					if (nwp != NULL) {
						nwp[0] = NULL;
						nwp++;
						for (k = 0; k < natoks; k++)
							nwp[k] = strsave(atoks[k], areanum);
						for (k = 1; k <= orig_cnt; k++)
							nwp[natoks + k - 1] = wp[k];
						wp = nwp;
						cp = *wp;
					}
				}
			}
		}

		/* strip all initial assignments */
		/* not correct wrt PATH=yyy command  etc */
		if (flag['x'])
			echo (cp ? wp: owp);
		if (cp == NULL && t->ioact == NULL) {
			while ((cp = *owp++) != NULL && assign(cp, COPYV))
				;
			return(setstatus(0));
		}
		else if (cp != NULL)
			shcom = inbuilt(cp);
	}
	t->words = wp;
	f = act;
	if (shcom == NULL && (f & FEXEC) == 0) {
		sh_save_cmd(t);
		i = parent();
		if (i != 0) {
			if (i == -1)
				return(rv);
			if (pin != NULL)
				closepipe(pin);
			return(pout==NULL? setstatus(waitfor(i,0)): 0);
		}
		if (talking) {
			signal(SIGINT, SIG_IGN);
			signal(SIGQUIT, SIG_IGN);
			resetsig = 1;
		}
		talking = 0;
		intr = 0;
		(*pforked)++;
		brklist = 0;
		execflg = 0;
	}
	if (owp != NULL)
		while ((cp = *owp++) != NULL && assign(cp, COPYV))
			if (shcom == NULL)
				export(lookup(cp));
#ifdef COMPIPE
	if ((pin != NULL || pout != NULL) && shcom != NULL && shcom != doexec) {
		err("piping to/from shell builtins not yet done");
		return(-1);
	}
#endif
	if (pin != NULL) {
		dup2(pin[0], 0);
		closepipe(pin);
	}
	if (pout != NULL) {
		dup2(pout[1], 1);
		closepipe(pout);
	}
	if ((iopp = t->ioact) != NULL) {
		if (shcom != NULL && shcom != doexec) {
			prs(cp);
			err(": cannot redirect shell command");
			return(-1);
		}
		while (*iopp)
			if (iosetup(*iopp++, pin!=NULL, pout!=NULL))
				return(rv);
	}
	if (shcom)
		return(setstatus((*shcom)(t)));
	/* should use FIOCEXCL */
	for (i=FDBASE; i<NOFILE; i++)
		close(i);
	if (resetsig) {
		signal(SIGINT, SIG_DFL);
		signal(SIGQUIT, SIG_DFL);
		signal(SIGTSTP, SIG_DFL);
	}
	if (t->type == TPAREN)
		exit(execute(t->left, NOPIPE, NOPIPE, FEXEC));
	if (wp[0] == NULL)
		exit(0);
	cp = rexecve(wp[0], wp, makenv());
	prs(wp[0]); prs(": "); warn(cp);
	if (!execflg)
		trap[0] = NULL;
	leave();
	/* NOTREACHED */
}

/*
 * common actions when creating a new child
 */
static int
parent()
{
	register int i;

	i = fork();
	if (i != 0) {
		if (i == -1)
			warn("try again");
	}
	return(i);
}

/*
 * 0< 1> are ignored as required
 * within pipelines.
 */
int
iosetup(iop, pipein, pipeout)
register struct ioword *iop;
int pipein, pipeout;
{
	register u;
	char *cp, *msg;

	if (iop->io_unit == IODEFAULT)	/* take default */
		iop->io_unit = iop->io_flag&(IOREAD|IOHERE)? 0: 1;
	if (pipein && iop->io_unit == 0)
		return(0);
	if (pipeout && iop->io_unit == 1)
		return(0);
	msg = iop->io_flag&(IOREAD|IOHERE)? "open": "create";
	if ((iop->io_flag & IOHERE) == 0) {
		cp = iop->io_name;
		if ((cp = evalstr(cp, DOSUB|DOTRIM)) == NULL)
			return(1);
	}
	if (iop->io_flag & IODUP) {
		if (cp[1] || (!digit(*cp) && *cp != '-')) {
			prs(cp);
			err(": illegal >& argument");
			return(1);
		}
		if (*cp == '-')
			iop->io_flag = IOCLOSE;
		iop->io_flag &= ~(IOREAD|IOWRITE);
	}
	switch (iop->io_flag) {
	case IOREAD:
		u = open(cp, 0);
		break;

	case IOHERE:
	case IOHERE|IOXHERE:
		u = herein(iop->io_name, iop->io_flag&IOXHERE);
		cp = "here file";
		break;

	case IOWRITE|IOCAT:
		if ((u = open(cp, 1)) >= 0) {
			lseek(u, (long)0, 2);
			break;
		}
	case IOWRITE:
		u = creat(cp, 0666);
		break;

	case IODUP:
		u = dup2(*cp-'0', iop->io_unit);
		break;

	case IOCLOSE:
		close(iop->io_unit);
		return(0);
	}
	if (u < 0) {
		prs(cp);
		prs(": cannot ");
		warn(msg);
		return(1);
	} else {
		if (u != iop->io_unit) {
			dup2(u, iop->io_unit);
			close(u);
		}
	}
	return(0);
}

static void
echo(wp)
register char **wp;
{
	register i;

	prs("+");
	for (i=0; wp[i]; i++) {
		if (i)
			prs(" ");
		prs(wp[i]);
	}
	prs("\n");
}

static struct op **
find1case(t, w)
struct op *t;
char *w;
{
	register struct op *t1;
	struct op **tp;
	register char **wp, *cp;

	if (t == NULL)
		return((struct op **)NULL);
	if (t->type == TLIST) {
		if ((tp = find1case(t->left, w)) != NULL)
			return(tp);
		t1 = t->right;	/* TPAT */
	} else
		t1 = t;
	for (wp = t1->words; *wp;)
		if ((cp = evalstr(*wp++, DOSUB)) && gmatch(w, cp))
			return(&t1->left);
	return((struct op **)NULL);
}

static struct op *
findcase(t, w)
struct op *t;
char *w;
{
	register struct op **tp;

	return((tp = find1case(t, w)) != NULL? *tp: (struct op *)NULL);
}

/*
 * Enter a new loop level (marked for break/continue).
 */
static void
brkset(bc)
struct brkcon *bc;
{
	bc->nextlev = brklist;
	brklist = bc;
}

/*
 * Wait for the last process created.
 * Print a message for each process found
 * that was killed by a signal.
 * Ignore interrupt signals while waiting
 * unless `canintr' is true.
 */
int
waitfor(lastpid, canintr)
register int lastpid;
int canintr;
{
	register int pid, rv;
	int s;
	int oheedint = heedint;

	heedint = 0;
	rv = 0;
	do {
		pid = waitpid(-1, &s, 2); /* 2 == WUNTRACED */
		if (pid == -1) {
			if (errno != EINTR || canintr)
				break;
		} else if ((s & 0xff) == 0x7f) {
			/* Child stopped (e.g. SIGTSTP from Ctrl+Z) */
			int jid = sh_add_job(pid, JOB_STOPPED, sh_last_cmd);
			if (talking) {
				sh_restore_tty();
				prs("\n["); prn(jid); prs("]+  Stopped                 ");
				prs(sh_last_cmd[0] ? sh_last_cmd : "job");
				prs(" (pid "); prn(pid); prs(")\n");
			}
			rv = 128 + ((s >> 8) & 0xff);
			if (pid == lastpid)
				break;
		} else {
			sh_remove_job(pid);
			if ((rv = WAITSIG(s)) != 0) {
				if (rv < NSIGNAL) {
					if (signame[rv] != NULL) {
						if (pid != lastpid) {
							prn(pid);
							prs(": ");
						}
						prs(signame[rv]);
					}
				} else {
					if (pid != lastpid) {
						prn(pid);
						prs(": ");
					}
					prs("Signal "); prn(rv); prs(" ");
				}
				if (WAITCORE(s))
					prs(" - core dumped");
				if (rv >= NSIGNAL || signame[rv])
					prs("\n");
				rv = -1;
			} else
				rv = WAITVAL(s);
		}
	} while (pid != lastpid);
	heedint = oheedint;
	if (intr)
		if (talking) {
			if (canintr)
				intr = 0;
		} else {
			if (exstat == 0) exstat = rv;
			onintr(0);
		}
	return(rv);
}

static void sh_reap_jobs(void)
{
	int pid, s;
	while ((pid = waitpid(-1, &s, 1)) > 0) { /* 1 == WNOHANG */
		if ((s & 0xff) != 0x7f)
			sh_remove_job(pid);
	}
}

static struct sh_job *sh_find_job(const char *arg, int prefer_state)
{
	int i, target = 0;
	struct sh_job *best = NULL;

	sh_reap_jobs();
	if (arg && *arg) {
		if (*arg == '%')
			arg++;
		target = getn((char *)arg);
		for (i = 0; i < MAX_JOBS; i++) {
			if (sh_jobs[i].state != JOB_NONE &&
			    (sh_jobs[i].jid == target || sh_jobs[i].pid == target))
				return &sh_jobs[i];
		}
		return NULL;
	}
	for (i = 0; i < MAX_JOBS; i++) {
		if (sh_jobs[i].state == prefer_state)
			best = &sh_jobs[i];
	}
	if (!best) {
		for (i = 0; i < MAX_JOBS; i++) {
			if (sh_jobs[i].state != JOB_NONE)
				best = &sh_jobs[i];
		}
	}
	return best;
}

int dojobs(struct op *t)
{
	int i;
	sh_reap_jobs();
	for (i = 0; i < MAX_JOBS; i++) {
		if (sh_jobs[i].state != JOB_NONE) {
			prs("["); prn(sh_jobs[i].jid); prs("]   ");
			prs(sh_jobs[i].state == JOB_STOPPED ? "Stopped                 " : "Running                 ");
			prs(sh_jobs[i].cmd);
			prs(" (pid "); prn(sh_jobs[i].pid); prs(")\n");
		}
	}
	return 0;
}

int dofg(struct op *t)
{
	struct sh_job *jb = sh_find_job(t ? t->words[1] : NULL, JOB_STOPPED);
	if (!jb) {
		err("fg: no current job");
		return 1;
	}
	strncpy(sh_last_cmd, jb->cmd, sizeof(sh_last_cmd) - 1);
	sh_last_cmd[sizeof(sh_last_cmd) - 1] = '\0';
	prs(jb->cmd); prs("\n");
	jb->state = JOB_RUNNING;
	kill(jb->pid, SIGCONT);
	return waitfor(jb->pid, 1);
}

int dobg(struct op *t)
{
	struct sh_job *jb = sh_find_job(t ? t->words[1] : NULL, JOB_STOPPED);
	if (!jb) {
		err("bg: no current job");
		return 1;
	}
	jb->state = JOB_RUNNING;
	kill(jb->pid, SIGCONT);
	prs("["); prn(jb->jid); prs("]+ ");
	prs(jb->cmd); prs(" &\n");
	return 0;
}

int
setstatus(s)
register int s;
{
	exstat = s;
	setval(lookup("?"), putn(s));
	return(s);
}

/*
 * PATH-searching interface to execve.
 * If getenv("PATH") were kept up-to-date,
 * execvp might be used.
 */
char *
rexecve(c, v, envp)
char *c, **v, **envp;
{
	register int i;
	register char *sp, *tp;
	int eacces = 0, asis = 0;

	sp = any('/', c)? "": path->value;
	asis = *sp == '\0';
	while (asis || *sp != '\0') {
		asis = 0;
		tp = e.linep;
		for (; *sp != '\0'; tp++)
			if ((*tp = *sp++) == ':') {
				asis = *sp == '\0';
				break;
			}
		if (tp != e.linep)
			*tp++ = '/';
		for (i = 0; (*tp++ = c[i++]) != '\0';)
			;
		execve(e.linep, v, envp);
		switch (errno) {
		case ENOEXEC:
			*v = e.linep;
			tp = *--v;
			*v = e.linep;
			execve("/bin/sh", v, envp);
			*v = tp;
			return("no Shell");

		case ENOMEM:
			return("program too big");

		case E2BIG:
			return("argument list too long");

		case EACCES:
			eacces++;
			break;
		}
	}
	return(errno==ENOENT ? "not found" : "cannot execute");
}

/*
 * Run the command produced by generator `f'
 * applied to stream `arg'.
 */
int
run(argp, f)
struct ioarg *argp;
int (*f)();
{
	struct op *otree;
	struct wdblock *swdlist;
	struct wdblock *siolist;
	jmp_buf ev, rt;
	xint *ofail;
	int rv;

	areanum++;
	swdlist = wdlist;
	siolist = iolist;
	otree = outtree;
	ofail = failpt;
	rv = -1;
	if (newenv(setjmp(errpt = ev)) == 0) {
		wdlist = 0;
		iolist = 0;
		pushio(argp, f);
		e.iobase = e.iop;
		yynerrs = 0;
		if (setjmp(failpt = rt) == 0 && yyparse() == 0)
			rv = execute(outtree, NOPIPE, NOPIPE, 0);
		quitenv();
	}
	wdlist = swdlist;
	iolist = siolist;
	failpt = ofail;
	outtree = otree;
	freearea(areanum--);
	return(rv);
}

