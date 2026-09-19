
#include <syscall.h>
#include <linux/errno.h>


int setpgid(int pid, int pgid)
{
	if (pid<0 || pgid<0)
                return -EINVAL;
        return syscall(__NR_setpgid, (long)pid, (long)pgid, 0);
}

