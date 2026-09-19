
#include <syscall.h>
#include <linux/errno.h>


int setuid(int uid)
{
	if (uid<0)
                return -EINVAL;
        return syscall(__NR_setuid, (long)uid, 0, 0);
}

