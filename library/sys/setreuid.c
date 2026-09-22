
#include <syscall.h>
#include <linux/errno.h>


int setreuid(int ruid, int euid)
{
	if (ruid<0 || euid<0)
                return __syscall_error(EINVAL);
        return __syscall_return(syscall(__NR_setreuid, (long)ruid, (long)euid, 0));
}

