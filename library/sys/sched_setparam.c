
#include <syscall.h>
#include <linux/errno.h>
#include <sched.h>


int sched_setparam(pid_t p, struct sched_param *param)
{
        return __syscall_return(syscall(__NR_sched_setparam, (long)p, (long)param, 0));
}

