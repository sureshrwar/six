
#include <six_proc.h>

	struct six_proc s;

main(int argc, char *argv[], char *envp[])
{
	s.index = 0;
	printf(" PID    PPID    NAME\n");
	while(!getproc(&s))
		printf(" %d      %d      %s\n", s.pid, s.ppid, s.comm);
}
