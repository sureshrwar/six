/*  fortune  -  hand out Chinese fortune cookies	Author: Bert Reuling */

#include <linux/types.h>
#include <stat.h>
#include <linux/time.h>
#include <stdlib.h>
#include <stdio.h>

#define COOKIEJAR "/usr/lib/fortunes"

static char *Copyright = "\0Copyright (c) 1990 Bert Reuling";
static unsigned long seed;

/*
 * Defined below but called from main(), so C89 gave it an implicit
 * "int magic()".  It returns unsigned long.
 */
unsigned long magic(unsigned long range);


int main(argc, argv)
int argc;
char *argv[];
{
  int c1, c2, c3;
  struct stat cookie_stat;
  FILE *cookie;

  if ((cookie = fopen(COOKIEJAR, "r")) == NULL) {
	printf("\nSome things better stay closed.\n  - %s\n", argv[0]);
	exit (-1);
  }

  /* Create seed from : date, time, user-id and process-id. we can't get
   * the position of the moon, unfortunately.
   */
  seed = time( (time_t *) 0) ^ (long) getuid() ^ (long) getpid();

  if (stat(COOKIEJAR, &cookie_stat) != 0) {
	printf("\nIt furthers one to see the super guru.\n  - %s\n", argv[0]);
	exit (-1);
  }
  fseek(cookie, magic((unsigned long) cookie_stat.st_size), 0); /* m ove bu magic... */

  c2 = c3 = '\n';
  while (((c1 = getc(cookie)) != EOF) && ((c1 != '%') || (c2 != '\n'))) {
	//c3 = c2;
	c2 = c1;
  }

  if (c1 == EOF) {
	printf("\nSomething unexpected has happened.\n  - %s\n\n", argv[0]);
	exit (-1);
  }

  c2 = c3 = '\n';
  while (((c1 = getc(cookie)) != '%') || (c2 != '\n')) {
	if (c1 == EOF) {
		rewind(cookie);
		continue;
	}
	putc(c2, stdout);
	//c3 = c2;
	c2 = c1;
  }
  putc('\n', stdout);
  fclose(cookie);
  return (0);
}

/*  magic  -  please study carefull: there is more than meets the eye */
unsigned long magic(range)
unsigned long range;
{

  seed = 9065531L * (seed % 9065533L) - 2 * (seed / 9065531L) + 1L;
  return (seed % range);
}
