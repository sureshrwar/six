
#include <syscall.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/utsname.h>


int uname(struct old_utsname *name)
{
	if (!name)
                return -EINVAL;
        return syscall(__NR_uname, (long)name, 0, 0);
}

