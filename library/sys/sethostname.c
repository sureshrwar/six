
#include <syscall.h>
#include <linux/errno.h>


int sethostname(char *name, int len)
{
	if (!name || len<0)
                return __syscall_error(EINVAL);
        return __syscall_return(syscall(__NR_sethostname, (long)name, (long)len, 0));
}

