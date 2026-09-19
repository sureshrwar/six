
#include <syscall.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/resource.h>


int wait4(pid_t pid, unsigned int *stat_addr, int options, struct rusage *ru)
{
        syscall(__NR_wait4, (long)pid, (long)stat_addr, (long)options);
        return syscall(__NR_wait4, (long)ru, 0, 0);
}

