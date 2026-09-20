#ifndef _SH_H
#define _SH_H
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <setjmp.h>
#include <linux/fcntl.h>
#include <linux/signal.h>
#include <linux/types.h>
#include <linux/limits.h>
#include <stat.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_PROMPT  "\\u@\\h:\\w\\$ "
#define ROOT_PROMPT     "\\u@\\h:\\w\\$ "

#define _PROTOTYPE(function, params)    function params
#define _ARGS(params)                   params

/* Need a way to have void used for ANSI, nothing for K&R. */
#ifndef _ANSI
#undef _VOID
#define _VOID
#endif

#define LINELIM 2100
#define NPUSH   8       /* limit to input nesting */

#define NOFILE  20      /* Number of open files */
#define NUFILE  10      /* Number of user-accessible files */
#define FDBASE  10      /* First file usable by Shell */

/*
 * values returned by wait
 */
#define WAITSIG(s) ((s)&0177)
#define WAITVAL(s) (((s)>>8)&0377)
#define WAITCORE(s) (((s)&0200)!=0)

/*
 * library and system defintions
 */
#ifdef __STDC__
typedef void xint;      /* base type of jmp_buf, for not broken compilers */
#else
typedef char * xint;    /* base type of jmp_buf, for broken compilers */
#endif


#define QUOTE   0200

#define NOBLOCK ((struct op *)NULL)
#define NOWORD  ((char *)NULL)
#define NOWORDS ((char **)NULL)
#define NOPIPE  ((int *)NULL)

/*
 * Description of a command or an operation on commands.
 * Might eventually use a union.
 */
struct op {
        int     type;   /* operation type, see below */
        char    **words;        /* arguments to a command */
        struct  ioword  **ioact;        /* IO actions (eg, < > >>) */
        struct op *left;
        struct op *right;
        char    *str;   /* identifier for case and for */
};

#define TCOM    1       /* command */
#define TPAREN  2       /* (c-list) */
#define TPIPE   3       /* a | b */
#define TLIST   4       /* a [&;] b */
#define TOR     5       /* || */
#define TAND    6       /* && */
#define TFOR    7
#define TDO     8
#define TCASE   9
#define TIF     10
#define TWHILE  11
#define TUNTIL  12
#define TELIF   13
#define TPAT    14      /* pattern in case */
#define TBRACE  15      /* {c-list} */
#define TASYNC  16      /* c & */

/*
 * actions determining the environment of a process
 */
#define BIT(i)  (1<<(i))
#define FEXEC   BIT(0)  /* execute without forking */

/*
 * flags to control evaluation of words
 */
#define DOSUB   1       /* interpret $, `, and quotes */
#define DOBLANK 2       /* perform blank interpretation */
#define DOGLOB  4       /* interpret [?* */
#define DOKEY   8       /* move words with `=' to 2nd arg. list */
#define DOTRIM  16      /* trim resulting string */

#define DOALL   (DOSUB|DOBLANK|DOGLOB|DOKEY|DOTRIM)

extern  char    **dolv;
extern  int     dolc;
extern  int     exstat;
extern  char    gflg;
extern  int     talking;        /* interactive (talking-type wireless) */
extern  int     execflg;
extern  int     multiline;      /* \n changed to ; */
extern  struct  op      *outtree;       /* result from parser */

extern  xint    *failpt;
extern  xint    *errpt;

struct  brkcon {
        jmp_buf brkpt;
        struct  brkcon  *nextlev;
} ;
extern  struct brkcon   *brklist;
extern  int     isbreak;

/*
 * redirection
 */
struct ioword {
        short   io_unit;        /* unit affected */
        short   io_flag;        /* action (below) */
        char    *io_name;       /* file name */
};
#define IOREAD  1       /* < */
#define IOHERE  2       /* << (here file) */
#define IOWRITE 4       /* > */
#define IOCAT   8       /* >> */
#define IOXHERE 16      /* ${}, ` in << */
#define IODUP   32      /* >&digit */
#define IOCLOSE 64      /* >&- */

#define IODEFAULT (-1)  /* token for default IO unit */

extern  struct  wdblock *wdlist;
extern  struct  wdblock *iolist;

/*
 * parsing & execution environment
 */
extern struct   env {
        char    *linep;
        struct  io      *iobase;
        struct  io      *iop;
        xint    *errpt;
        int     iofd;
        struct  env     *oenv;
} e;

/*
 * flags:
 * -e: quit on error
 * -k: look for name=value everywhere on command line
 * -n: no execution
 * -t: exit after reading and executing one command
 * -v: echo as read
 * -x: trace
 * -u: unset variables net diagnostic
 */
extern  char    *flag;

extern  char    *null;  /* null value for variable */
extern  int     intr;   /* interrupt pending */

extern  char    *trap[_NSIG+1];
extern  char    ourtrap[_NSIG+1];
extern  int     trapset;        /* trap pending */

extern  int     heedint;        /* heed interrupt signals */

extern  int     yynerrs;        /* yacc */

extern  char    line[LINELIM];
extern  char    *elinep;

/*
 * other functions      
 */
#ifdef __STDC__
int (*inbuilt(char *s ))(void);
#else
int (*inbuilt())();
#endif
_PROTOTYPE(char *rexecve , (char *c , char **v , char **envp ));
_PROTOTYPE(char *space , (int n ));
_PROTOTYPE(char *strsave , (char *s , int a ));
_PROTOTYPE(char *evalstr , (char *cp , int f ));
_PROTOTYPE(char *putn , (int n ));
_PROTOTYPE(char *itoa , (unsigned u , int n ));
_PROTOTYPE(char *unquote , (char *as ));
_PROTOTYPE(struct var *lookup , (char *n ));
_PROTOTYPE(int rlookup , (char *n ));
_PROTOTYPE(struct wdblock *glob , (char *cp , struct wdblock *wb ));
_PROTOTYPE(int subgetc , (int ec , int quoted ));
_PROTOTYPE(char **makenv , (void));
_PROTOTYPE(char **eval , (char **ap , int f ));
_PROTOTYPE(int setstatus , (int s ));
_PROTOTYPE(int waitfor , (int lastpid , int canintr ));
 
_PROTOTYPE(void onintr , (int s )); /* SIGINT handler */
 
_PROTOTYPE(int newenv , (int f ));
_PROTOTYPE(void quitenv , (void));
_PROTOTYPE(void err , (char *s ));
_PROTOTYPE(int anys , (char *s1 , char *s2 ));
_PROTOTYPE(int any , (int c , char *s ));
_PROTOTYPE(void next , (int f ));
_PROTOTYPE(void setdash , (void));
_PROTOTYPE(void onecommand , (void));
_PROTOTYPE(void runtrap , (int i ));
_PROTOTYPE(void xfree , (char *s ));
_PROTOTYPE(int letter , (int c ));
_PROTOTYPE(int digit , (int c ));
_PROTOTYPE(int letnum , (int c ));
_PROTOTYPE(int gmatch , (char *s , char *p ));
/*
 * error handling
 */
_PROTOTYPE(void leave , (void)); /* abort shell (or fail in subshell) */
_PROTOTYPE(void fail , (void));  /* fail but return to process next command */
_PROTOTYPE(void warn , (char *s ));      
_PROTOTYPE(void sig , (int i ));         /* default signal handler */
_PROTOTYPE(void prs_prompt , (void));

#endif // _SH_H
