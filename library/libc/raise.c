
#include <stdio.h>


int raise(int sig)
{
	int pid;
	pid = getpid();
	return kill(pid, sig);
}       

