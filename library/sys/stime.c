
#include <syscall.h>
#include <linux/errno.h>


int stime(int *t)
{
	if (!t)
                return __syscall_error(EINVAL);
        return __syscall_return(syscall(__NR_stime, (long)t, 0, 0));
}


