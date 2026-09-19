
#include <syscall.h>
#include <linux/errno.h>
#include <linux/utsname.h>


int olduname(struct oldold_utsname *name)
{
	if (!name)
        	return -EINVAL;
        return syscall(__NR_olduname, (long)name, 0, 0);
}

