
#include <syscall.h>
#include <linux/errno.h>


int setgroups(int gidsetsize, int *glist)
{
	if (gidsetsize<0 || !glist)
                return __syscall_error(EINVAL);
        return __syscall_return(syscall(__NR_setgroups, (long)gidsetsize, (long)glist, 0));
}

