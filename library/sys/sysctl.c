
#include <syscall.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/sysctl.h>


int sysctl(struct __sysctl_args *args)
{
        if (!args)
                return -EINVAL;
        return syscall(__NR__sysctl, (long)args, 0, 0);
}

