
#include <syscall.h>
#include <errno.h>


int acct(char *name)
{
	int ret;
        ret =  syscall(__NR_acct, (long)name, 0, 0);
        if (ret<0)
	{
		errno = -ret;
		return -1;
	}
	else return 0;
}

