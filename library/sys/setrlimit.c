
#include <syscall.h>
#include <linux/errno.h>
#include <linux/resource.h>


int setrlimit(int resource, struct rlimit *r)
{
	if (resource<0 || !r)
                return -EINVAL;
        return syscall(__NR_setrlimit, (long)resource, (long)r, 0);
}

