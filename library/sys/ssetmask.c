
#include <syscall.h>
#include <linux/errno.h>


int ssetmask(int mask)
{
	if (mask<0)
                return __syscall_error(EINVAL);
        return __syscall_return(syscall(__NR_ssetmask, (long)mask, 0, 0));
}

