
#include <syscall.h>
#include <linux/errno.h>


int sched_yield()
{
        return __syscall_return(syscall(__NR_sched_yield, 0, 0, 0));
}

