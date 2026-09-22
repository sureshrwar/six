
#include <syscall.h>
#include <linux/errno.h>
#include <linux/resource.h>


int getrusage(int who, struct rusage *r)
{
	if (who<0 || !r)
                return __syscall_error(EINVAL);
        return __syscall_return(syscall(__NR_getrusage, (long)who, (long)r, 0));
}

