
#include <syscall.h>
#include <linux/errno.h>
#include <linux/utsname.h>


int olduname(struct oldold_utsname *name)
{
	if (!name)
        	return __syscall_error(EINVAL);
        return __syscall_return(syscall(__NR_olduname, (long)name, 0, 0));
}

