
#include <syscall.h>
#include <linux/errno.h>
#include <linux/sched.h>


int sched_setscheduler(pid_t p, int policy, struct sched_param *param)
{
        return syscall(__NR_sched_setscheduler, (long)p, (long)policy, (long)param);
}

