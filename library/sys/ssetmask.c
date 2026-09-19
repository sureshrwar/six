
#include <syscall.h>
#include <linux/errno.h>


int ssetmask(int mask)
{
	if (mask<0)
                return -EINVAL;
        return syscall(__NR_ssetmask, (long)mask, 0, 0);
}

