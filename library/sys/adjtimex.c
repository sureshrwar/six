
#include <syscall.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/timex.h>


int adjtimex(struct timex *t)
{
	int ret;
        ret =  syscall(__NR_adjtimex, (long)t, 0, 0);
	return __syscall_return(ret);
}

