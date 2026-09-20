/* kill - send a signal to a process	Author: Adri Koppes  */

#include <linux/types.h>
#include <errno.h>
#include <linux/signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


int main(argc, argv)
int argc;
char **argv;
{
  pid_t proc;
  int ex = 0, signal = SIGTERM;
  char *end;
  long l;
  unsigned long ul;

  if (argc < 2) usage();
  if (argc > 1 && *argv[1] == '-') {
	ul = strtoul(argv[1] + 1, &end, 10);
	if (end == argv[1] + 1 || *end != 0 || ul > _NSIG) usage();
	signal = ul;
	argv++;
	argc--;
  }
  while (--argc) {
	argv++;
	l = strtoul(*argv, &end, 10);
	if (end == *argv || *end != 0 || (pid_t) l != l) usage();
	proc = l;
	if (kill(proc, signal) < 0) {
		fprintf(stderr, "kill: %d: %s\n", proc, strerror(errno));
		ex = 1;
	}
  }
  return(ex);
}

void usage()
{
  fprintf(stderr, "Usage: kill [-sig] pid\n");
  exit(1);
}
