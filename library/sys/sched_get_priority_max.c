
#include <syscall.h>
#include <linux/errno.h>


int sched_get_priority_max(int policy)
{
        return __syscall_return(syscall(__NR_sched_get_priority_max, (long)policy, 0, 0));
}

