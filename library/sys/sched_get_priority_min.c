
#include <syscall.h>
#include <linux/errno.h>


int sched_get_priority_min(int policy)
{
        return syscall(__NR_sched_get_priority_min, (long)policy, 0, 0);
}

