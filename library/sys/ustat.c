
#include <syscall.h>
#include <linux/errno.h>
#include <linux/types.h>


int ustat(long dev, struct ustat *u)
{
	if (dev<0 || !u)
                return __syscall_error(EINVAL);
        return __syscall_return(syscall(__NR_ustat, (long)dev, (long)u, 0));
}

