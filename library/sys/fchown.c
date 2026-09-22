
#include <syscall.h>
#include <linux/errno.h>
#include <linux/types.h>


int fchown(int fd, uid_t u, gid_t g)
{
	if (fd<0)
                return __syscall_error(EINVAL);
        return __syscall_return(syscall(__NR_fchown, (long)fd, (long)u, (long)g));
}

