
#include <syscall.h>
#include <linux/errno.h>
#include <linux/resource.h>


int getrlimit(int resource, struct rlimit *r)
{
	if (resource<0 || !r)
                return __syscall_error(EINVAL);
        return __syscall_return(syscall(__NR_getrlimit, (long)resource, (long)r, 0));
}

