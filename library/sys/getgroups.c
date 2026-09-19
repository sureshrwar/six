
#include <syscall.h>
#include <linux/errno.h>


int getgroups(int gidsetsize, int *glist)
{
	if (gidsetsize<0 || !glist)
                return -EINVAL;
        return syscall(__NR_getgroups, (long)gidsetsize, (long)glist, 0);
}

