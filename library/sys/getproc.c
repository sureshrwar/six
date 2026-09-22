
#include <syscall.h>
#include <linux/errno.h>


int getproc(void *buf)
{
	if (!buf)
                return __syscall_error(EINVAL);
	return __syscall_return(syscall(__NR_ps, (long)buf, (long)0, (long)0));
}
