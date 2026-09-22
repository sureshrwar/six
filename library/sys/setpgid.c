
#include <syscall.h>
#include <linux/errno.h>


int setpgid(int pid, int pgid)
{
	if (pid<0 || pgid<0)
                return __syscall_error(EINVAL);
        return __syscall_return(syscall(__NR_setpgid, (long)pid, (long)pgid, 0));
}

