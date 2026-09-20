#include <syscall.h>
#include <linux/errno.h>
#include <errno.h>
#include <linux/types.h>
#include <linux/time.h>

extern int syscall5(int num, long one, long two, long three, long four, long five);

int select(int n, fd_set *inp, fd_set *outp, fd_set *exp, struct timeval *tvp)
{
	int ret;
	if (n < 0) {
		errno = EINVAL;
		return -1;
	}
	ret = syscall5(__NR__newselect, (long)n, (long)inp, (long)outp, (long)exp, (long)tvp);
	if (ret < 0) {
		errno = -ret;
		return -1;
	}
	return ret;
}
