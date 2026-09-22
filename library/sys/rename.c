
#include <syscall.h>
#include <linux/errno.h>


int rename(char *old, char *new)
{
	if (!old || !new)
                return __syscall_error(EINVAL);
        return __syscall_return(syscall(__NR_rename, (long)old, (long)new, 0));
}


