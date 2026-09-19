
#include <syscall.h>
#include <linux/errno.h>
#include <sched.h>


int sched_getscheduler(pid_t p)
{
        return syscall(__NR_sched_getscheduler, (long)p, 0, 0);
}

