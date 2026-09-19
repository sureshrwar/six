
#include <syscall.h>
#include <linux/errno.h>
#include <linux/sched.h>


int sched_getparam(pid_t p, struct sched_param *param)
{
        return syscall(__NR_sched_getparam, (long)p, (long)param, 0);
}

