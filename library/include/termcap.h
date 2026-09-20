#ifndef _TERMCAP_H
#define _TERMCAP_H

int tgetent(char *bp, char *name);
int tgetnum(char *id);
int tgetflag(char *id);
char *tgetstr(char *id, char **area);
char *tgoto(char *cm, int destcol, int destline);
int tputs(char *cp, int affcnt, int (*outc)(int));

#endif
