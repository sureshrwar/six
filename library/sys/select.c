
#include <syscall.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/time.h>


int select(int n, fd_set *inp, fd_set *outp, fd_set *exp, struct timeval *tvp)
{
	if (n<0) 
                return -EINVAL;
        syscall(__NR_select, (long)n, (long)inp, (long)outp);
        return syscall(__NR_select, (long)exp, (long)tvp, 0);
}

