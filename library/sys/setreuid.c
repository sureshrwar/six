
#include <syscall.h>
#include <linux/errno.h>


int setreuid(int ruid, int euid)
{
	if (ruid<0 || euid<0)
                return -EINVAL;
        return syscall(__NR_setreuid, (long)ruid, (long)euid, 0);
}

