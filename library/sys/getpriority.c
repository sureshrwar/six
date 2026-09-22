
#include <syscall.h>
#include <linux/errno.h>


int getpriority(int which, int who)
{
	if (which<0 || who<0)
                return __syscall_error(EINVAL);
        return __syscall_return(syscall(__NR_getpriority, (long)which, (long)who, 0));
}

