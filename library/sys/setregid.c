
#include <syscall.h>
#include <linux/errno.h>


int setregid(int rgid, int egid)
{
	if (rgid<0 || egid<0)
                return -EINVAL;
        return syscall(__NR_setregid, (long)rgid, (long)egid, 0);
}

