
#include <syscall.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/time.h>


int sched_rr_get_interval(pid_t p, struct timespec *interval)
{
        if (!interval)
                return -EINVAL;
        return syscall(__NR_sched_rr_get_interval, (long)p, (long)interval, 0);
}

