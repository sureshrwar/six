
#include <syscall.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/kernel.h>


int sysinfo(struct sysinfo *info)
{
	if (!info)
                return __syscall_error(EINVAL);
        return __syscall_return(syscall(__NR_sysinfo, (long)info, 0, 0));
}

