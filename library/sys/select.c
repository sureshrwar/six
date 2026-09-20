#include <syscall.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/time.h>

int select(int n, fd_set *inp, fd_set *outp, fd_set *exp, struct timeval *tvp)
{
	unsigned long args[5];
	if (n < 0) 
		return -EINVAL;
	args[0] = (unsigned long)n;
	args[1] = (unsigned long)inp;
	args[2] = (unsigned long)outp;
	args[3] = (unsigned long)exp;
	args[4] = (unsigned long)tvp;
	return syscall(__NR_select, (long)args);
}
