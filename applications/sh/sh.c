#include "sh.h"
#include "io.h"
#include "var.h"
#include "area.h"


int     intr;
int     inparse;
char    flags['z'-'a'+1];
char    *flag = flags-'a';
char    *elinep = line+sizeof(line)-5;
char    *null   = "";
int     heedint =1;
struct  env     e ={line, iostack, iostack-1,
                    (xint *)NULL, FDBASE, (struct env *)NULL};

extern  char    **envlist;      /* envlistment pointer */

/*
 * default shell, search rules
 */
char    shellname[] = "/bin/sh";
char    search[] = ":/bin:/usr/bin";

_PROTOTYPE(void (*qflag), (int)) = SIG_IGN;

_PROTOTYPE(int main, (int argc, char **argv ));
_PROTOTYPE(int newfile, (char *s ));
_PROTOTYPE(static char *findeq, (char *cp ));
_PROTOTYPE(static char *cclass, (char *p, int sub ));
_PROTOTYPE(void initarea, (void));

int main(argc, argv)
int argc;
register char **argv;
{
        register int f;
        register char *s;
        int cflag;
        char *name, **ap;
        int (*iof)();
        initarea();

        if ((ap = envlist) != NULL) {
                while (*ap)
                        assign(*ap++, !COPYV);
                for (ap = envlist; *ap;)
                        export(lookup(*ap++));
        }
        closeall();

        areanum = 1;

        shell = lookup("SHELL");
        if (shell->value == null)
                setval(shell, shellname);
        export(shell);


        homedir = lookup("HOME");
        if (homedir->value == null)
                setval(homedir, "/");
        export(homedir);

        setval(lookup("$"), itoa(getpid(), 5));

        path = lookup("PATH");
        if (path->value == null)
                setval(path, search);
        export(path);

        ifs = lookup("IFS");
        if (ifs->value == null)
                setval(ifs, " \t\n");

        prompt = lookup("PS1");
        if (prompt->value == null)
                setval(prompt, DEFAULT_PROMPT);

        if (geteuid() == 0) {
                setval(prompt, ROOT_PROMPT);
                prompt->status &= ~EXPORT;
        }
        cprompt = lookup("PS2");
        if (cprompt->value == null)
                setval(cprompt, "> ");

        iof = filechar;
        cflag = 0;
        name = *argv++;
        if (--argc >= 1) {
                if(argv[0][0] == '-' && argv[0][1] != '\0') {
                        for (s = argv[0]+1; *s; s++)
                                switch (*s) {
                                case 'c':
                                        prompt->status &= ~EXPORT;
                                        cprompt->status &= ~EXPORT;
                                        setval(prompt, "");
                                        setval(cprompt, "");
                                        cflag = 1;
                                        if (--argc > 0)
                                                PUSHIO(aword, *++argv, iof = nlchar);
                                        break;
        
                                case 'q':
                                        qflag = SIG_DFL;
                                        break;
                
                                case 's':
                                        /* standard input */
                                        break;

                                case 't':
                                        prompt->status &= ~EXPORT;
                                        setval(prompt, "");
                                        iof = linechar;
                                        break;
        
                                case 'i':
                                        talking++;
                                default:
                                        if (*s>='a' && *s<='z')
                                                flag[*s]++;
                                }
                } else {
                        argv--;
                        argc++;
                }
                if (iof == filechar && --argc > 0) {
                        setval(prompt, "");
                        setval(cprompt, "");
                        prompt->status &= ~EXPORT;
                        cprompt->status &= ~EXPORT;
                        if (newfile(name = *++argv))
                                exit(1);
                }
        }
        setdash();
        if (e.iop < iostack) {
                PUSHIO(afile, 0, iof);
                if (isatty(0) && isatty(1) && !cflag)
                        talking++;
        }
        signal(SIGQUIT, qflag);
        if (name && name[0] == '-') {
                talking++;
                if ((f = open(".profile", 0)) >= 0)
                        next(remap(f));
                if ((f = open("/etc/profile", 0)) >= 0)
                        next(remap(f));
        }
        if (talking)
                signal(SIGTERM, sig);
        if (signal(SIGINT, SIG_IGN) != SIG_IGN)
                signal(SIGINT, onintr);
        dolv = argv;
        dolc = argc;
        //dolv[0] = name;
        if (dolc > 1)
                for (ap = ++argv; --argc > 0;)
                        if (assign(*ap = *argv++, !COPYV))
                                dolc--; /* keyword */
                        else
                                ap++;
        setval(lookup("#"), putn((--dolc < 0) ? (dolc = 0) : dolc));

        for (;;) {
                if (talking && e.iop <= iostack)
                        prs_prompt();
                onecommand();
        }
}

void
setdash()
{
        register char *cp, c;
        char m['z'-'a'+1];

        cp = m;
        for (c='a'; c<='z'; c++)
                if (flag[c])
                        *cp++ = c;
        *cp = 0;
        setval(lookup("-"), m);
}               
                
int                     
newfile(s)
register char *s;
{       
        register f;
        if (strcmp(s, "-") != 0) {
                f = open(s, 0);
                if (f < 0) {
                        prs(s);
                        err(": cannot open");
                        return(1);
                }
        } else  
                f = 0;
        next(remap(f));
        return(0);
}       
                
void                    
onecommand()                    
{                       
        register i;             
        jmp_buf m1;

        while (e.oenv)
                quitenv();
        areanum = 1;    
        freehere(areanum);
        freearea(areanum);
        garbage();
        wdlist = 0;
        iolist = 0;
        e.errpt = 0;
        e.linep = line;
        yynerrs = 0;
        multiline = 0;
        inparse = 1;
        intr = 0;
        execflg = 0;
        setjmp(failpt = m1);    /* Bruce Evans' fix */
        if (setjmp(failpt = m1) || yyparse() || intr) {
                while (e.oenv)
                        quitenv();
                scraphere();
                if (!talking && intr)
                        leave();
                inparse = 0;
                intr = 0;
                return;
        }
        inparse = 0;
        brklist = 0;
        intr = 0;
        execflg = 0;
        if (!flag['n'])
                execute(outtree, NOPIPE, NOPIPE, 0);
        if (!talking && intr) {
                execflg = 0;
                leave();
        }
        if ((i = trapset) != 0) {
                trapset = 0;
                runtrap(i);
        }
}

void
fail()
{
        longjmp(failpt, 1);
        /* NOTREACHED */
}

void
leave()
{
        if (execflg)
                fail();
        scraphere();
        freehere(1);
        runtrap(0);
        exit(exstat);
        /* NOTREACHED */
}

void
warn(s)
register char *s;
{                       
        if(*s) {
                prs(s);
                exstat = -1;
        }       
        prs("\n");
        if (flag['e'])
                leave();
}       
        
void    
err(s)  
char *s;
{               
        warn(s);
        if (flag['n'])
                return;
        if (!talking)
                leave();
        if (e.errpt)
                longjmp(e.errpt, 1);
        closeall();
        e.iop = e.iobase = iostack;
}

int
newenv(f)
int f;  
{       
        register struct env *ep;

        if (f) {
                quitenv();
                return(1);
        }
        ep = (struct env *) space(sizeof(*ep));
        if (ep == NULL) {
                while (e.oenv)
                        quitenv();
                fail();
        }
        *ep = e;
        e.oenv = ep;
        e.errpt = errpt;
        return(0);
}
void
quitenv()
{
        register struct env *ep;
        register fd;

        if ((ep = e.oenv) != NULL) {
                fd = e.iofd;
                e = *ep;
                /* should close `'d files */
                DELETE(ep);
                while (--fd >= e.iofd)
                        close(fd);
        }
}

/*
 * Is any character from s1 in s2?
 */
int
anys(s1, s2)
register char *s1, *s2;
{
        while (*s1)
                if (any(*s1++, s2))
                        return(1);
        return(0);
}

/*
 * Is character c in s?
 */
int
any(c, s)
register int c;
register char *s;
{
        while (*s)
                if (*s++ == c)
                        return(1);
        return(0);
}

char *
putn(n)
register int n;
{
        return(itoa(n, -1));
}
        
char *  
itoa(u, n)
register unsigned u;
int n;          
{               
        register char *cp;
        static char s[20];
        int m;  
                        
        m = 0;
        if (n < 0 && (int) u < 0) {
                m++;
                u = -u;
        } 
        cp = s+sizeof(s);
        *--cp = 0;
        do {
                *--cp = u%10 + '0';
                u /= 10;
        } while (--n > 0 || u);
        if (m)  
                *--cp = '-';
        return(cp);
}

void
next(f)
int f;
{
        PUSHIO(afile, f, filechar);
}

void sh_restore_tty(void);

void
onintr(s)
int s;                          /* ANSI C requires a parameter */
{                       
        signal(SIGINT, onintr);
        intr = 1;
        if (talking) {
                sh_restore_tty();
                if (inparse) {
                        prs("\n");
                        fail();
                }
        }
        else if (heedint) {
                execflg = 0;
                leave();
        }
}

int
letter(c)
register c;
{
        return((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_');
}

int
digit(c)
register c;
{
        return(c >= '0' && c <= '9');
}

int
letnum(c)
register c;
{
        return(letter(c) || digit(c));
}

char *
space(n)
int n;
{
        register char *cp;

        if ((cp = getcell(n)) == 0)
                err("out of string space");
        return(cp);
}

char *
strsave(s, a)
register char *s;
int a;
{
        register char *cp, *xp;
                
        if ((cp = space(strlen(s)+1)) != NULL) {
                setarea((char *)cp, a);
                for (xp = cp; (*xp++ = *s++) != '\0';)
                        ;
                return(cp);
        }
        return("");
}

void
xfree(s)
register char *s; 
{
        DELETE(s);
}

/*
 * trap handling
 */     
void
sig(i)
register int i;
{
        trapset = i;
        signal(i, sig);
}       

void runtrap(i)
int i;
{
        char *trapstr;

        if ((trapstr = trap[i]) == NULL)
                return;
        if (i == 0)
                trap[i] = 0;
        RUN(aword, trapstr, nlchar);
}

