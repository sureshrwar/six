#include <errno.h>
#include <stdio.h>

#define reverse()       write(1, SO, strlen(SO))        /* reverse video */
#define normal()        write(1, SE, strlen(SE))        /* undo reverse() */
#define clearln()       write(1,"\r",1); \
                write(1, CD, strlen(CD))        /* clear line */

#define  TC_BUFFER  1024        /* Size of termcap(3) buffer     */
  
char *SO, *SE, *CD;
char buffer[TC_BUFFER];
char clear[30];
char *p = &clear[0];

int get_termcap()
{
  static char termbuf[50];
  char *loc = termbuf;
  char entry[1024];
  char *term;


  if ((term = getenv("TERM")) == NULL) {
        return Error("no $TERM defined");
  }
  if (tgetent(entry, getenv("TERM")) <= 0) {
        return Error("Unknown terminal.");
  }
  if (tgetent(buffer, term) != 1) {
        return Error("No termcap definition for $TERM");
  }
  if ((tgetstr("cl", &p)) == NULL) {
        return Error("No clear (cl) entry for $TERM");
  }
  SO = tgetstr("so", &loc);
  SE = tgetstr("se", &loc);
  CD = tgetstr("cd", &loc);

  if (CD == (char *) 0) CD = "             \r";
  return 0;
}

int Error(str)
char *str;
{
  fprintf(stderr, "clr: %s\n", str);
  return -1;
}


int clrscr()
{
  if (get_termcap() < 0)
	return -1;

  /* Clear the screen  */
  normal();
  clearln();
  printf("%s", clear);

  return(0);
}
