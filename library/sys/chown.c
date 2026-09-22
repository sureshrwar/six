
#include <syscall.h>
#include <linux/errno.h>


int chown(char *name, int user, int group)
{
	if (!name || user<0 || group<0)
                return __syscall_error(EINVAL);
        return __syscall_return(syscall(__NR_chown, (long)name, (long)user, (long)group));
}


