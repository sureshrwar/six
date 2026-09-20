#include <syscall.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/time.h>

extern int syscall5(int num, long one, long two, long three, long four, long five);

int select(int n, fd_set *inp, fd_set *outp, fd_set *exp, struct timeval *tvp)
{
	if (n < 0) 
		return -EINVAL;
	return syscall5(__NR__newselect, (long)n, (long)inp, (long)outp, (long)exp, (long)tvp);
}
