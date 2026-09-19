
#include <syscall.h>
#include <linux/errno.h>


int umask(int mask)
{
	if (mask<0)
                return -EINVAL;
        return syscall(__NR_umask, (long)mask, 0, 0);
}

