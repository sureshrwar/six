
#include <syscall.h>
#include <linux/errno.h>


int dup2(int ofd, int nfd)
{
	if (ofd<0 || nfd<0)
                return __syscall_error(EINVAL);
        return __syscall_return(syscall(__NR_dup2, (long)ofd, (long)nfd, 0));
}

