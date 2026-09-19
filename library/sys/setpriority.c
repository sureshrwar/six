
#include <syscall.h>
#include <linux/errno.h>


int setpriority(int which, int who, int niceval)
{
	if (which<0 || who<0)
                return -EINVAL;
        return syscall(__NR_setpriority, (long)which, (long)who, (long)niceval);
}

