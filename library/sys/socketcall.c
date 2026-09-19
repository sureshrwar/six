
#include <syscall.h>
#include <linux/errno.h>


int socketcall(int call, unsigned long *args)
{
	if (!args)
		return -EINVAL;
        return syscall(__NR_socketcall, (long)call, (long)args, 0);
}

