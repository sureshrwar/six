#include "sh.h"
#include "var.h"
#include "io.h"
#include "area.h"

struct ioarg ioargstack[NPUSH];
struct  io      iostack[NPUSH];
char    **dolv;
int     dolc;
int     exstat;
char    gflg;
int     talking;        /* interactive (talking-type wireless) */
int     execflg;
int     multiline;      /* \n changed to ; */
struct  op      *outtree;       /* result from parser */
xint    *failpt;
xint    *errpt;
struct brkcon   *brklist;
int     isbreak;
struct wdblock *wdlist;
struct  wdblock *iolist;
char    *trap[_NSIG+1];
char    ourtrap[_NSIG+1];
int     trapset;        /* trap pending */
int     yynerrs;        /* yacc */
char    line[LINELIM];
struct  var     *vlist;         /* dictionary */
struct  var     *homedir;       /* home directory */
struct  var     *prompt;        /* main prompt */
struct  var     *cprompt;       /* continuation prompt */
struct  var     *path;          /* search path for commands */
struct  var     *shell;         /* shell to interpret command files */
struct  var     *ifs;           /* field separators */
struct ioarg ioargstack[NPUSH];
struct  io      iostack[NPUSH];
int     areanum;        /* current allocation area */
