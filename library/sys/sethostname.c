
#include <syscall.h>
#include <linux/errno.h>


int sethostname(char *name, int len)
{
	if (!name || len<0)
                return -EINVAL;
        return syscall(__NR_sethostname, (long)name, (long)len, 0);
}

