#ifndef _IO_H
#define _IO_H
struct sh_iobuf {
  unsigned id;                          /* buffer id */
  char buf[512];                        /* buffer */
  char *bufp;                           /* pointer into buffer */
  char *ebufp;                          /* pointer to end of buffer */
};

/* possible arguments to an IO function */
struct ioarg {
        char    *aword;
        char    **awordlist;
        int     afile;          /* file descriptor */
        unsigned afid;          /* buffer id */
        long    afpos;          /* file position */
        struct sh_iobuf *afbuf; /* buffer for this file */
};
extern struct ioarg ioargstack[NPUSH];
#define AFID_NOBUF      (~0)
#define AFID_ID         0

/* an input generator's state */
struct  io {
        int     (*iofn)(/*_VOID*/);
        struct  ioarg   *argp;
        int     peekc;
        char    prev;           /* previous character read by readc() */
        char    nlcount;        /* for `'s */
        char    xchar;          /* for `'s */
        char    task;           /* reason for pushed IO */
};
extern  struct  io      iostack[NPUSH];
#define XOTHER  0       /* none of the below */
#define XDOLL   1       /* expanding ${} */
#define XGRAVE  2       /* expanding `'s */
#define XIO     3       /* file IO */

/* in substitution */
#define INSUB() (e.iop->task == XGRAVE || e.iop->task == XDOLL)

/*
 * input generators for IO structure
 */
_PROTOTYPE(int nlchar , (struct ioarg *ap ));
_PROTOTYPE(int strchar , (struct ioarg *ap ));
_PROTOTYPE(int qstrchar , (struct ioarg *ap ));
_PROTOTYPE(int filechar , (struct ioarg *ap ));
_PROTOTYPE(int herechar , (struct ioarg *ap ));
_PROTOTYPE(int linechar , (struct ioarg *ap ));
_PROTOTYPE(int gravechar , (struct ioarg *ap , struct io *iop ));
_PROTOTYPE(int qgravechar , (struct ioarg *ap , struct io *iop ));
_PROTOTYPE(int dolchar , (struct ioarg *ap ));
_PROTOTYPE(int wdchar , (struct ioarg *ap ));
_PROTOTYPE(void scraphere , (void));
_PROTOTYPE(void freehere , (int area ));
_PROTOTYPE(void gethere , (void));
_PROTOTYPE(void markhere , (char *s , struct ioword *iop ));
_PROTOTYPE(int herein , (char *hname , int xdoll ));
_PROTOTYPE(int run , (struct ioarg *argp , int (*f)(_VOID)));

/*
 * IO functions
 */
int eofc();
int sh_getc(int ec);
int readc();
_PROTOTYPE(void unget , (int c ));
_PROTOTYPE(void ioecho , (int c ));
_PROTOTYPE(void prs , (char *s ));
_PROTOTYPE(void sh_putc , (int c ));
_PROTOTYPE(void prn , (unsigned u ));
_PROTOTYPE(void closef , (int i ));
_PROTOTYPE(void closeall , (void));

/*
 * IO control
 */
_PROTOTYPE(void pushio , (struct ioarg *argp , int (*fn)(_VOID)));
_PROTOTYPE(int remap , (int fd ));
_PROTOTYPE(int openpipe , (int *pv ));
_PROTOTYPE(void closepipe , (int *pv ));
_PROTOTYPE(struct io *setbase , (struct io *ip ));

extern  struct  ioarg   temparg;        /* temporary for PUSHIO */
#define PUSHIO(what,arg,gen) ((temparg.what = (arg)),pushio(&temparg,(gen)))
#define RUN(what,arg,gen) ((temparg.what = (arg)), run(&temparg,(gen)))

#endif

