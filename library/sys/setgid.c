
#include <syscall.h>
#include <linux/errno.h>


int setgid(int gid)
{
	if (gid<0)
                return -EINVAL;
        return syscall(__NR_setgid, (long)gid, 0, 0);
}

