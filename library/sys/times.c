
#include <syscall.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/times.h>

int times(struct tms *t)
{
        return __syscall_return(syscall(__NR_times, (long)t, 0, 0));
}

