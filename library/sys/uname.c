#include <syscall.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/utsname.h>

int uname(struct new_utsname *name)
{
	if (!name)
		return __syscall_error(EINVAL);
	return __syscall_return(syscall(__NR_uname, (long)name, 0, 0));
}
