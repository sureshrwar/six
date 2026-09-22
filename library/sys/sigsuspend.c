
#include <syscall.h>
#include <linux/errno.h>
#include <linux/signal.h>


int sigsuspend(sigset_t set)
{
        return __syscall_return(syscall(__NR_sigsuspend, (long)set, 0, 0));
}

