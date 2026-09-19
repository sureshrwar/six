
#include <syscall.h>
#include <linux/errno.h>


int setdomainname(char *name, int len)
{
	if (!name || len<0)
                return -EINVAL;
        return syscall(__NR_setdomainname, (long)name, (long)len, 0);
}

