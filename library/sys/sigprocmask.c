
#include <syscall.h>
#include <linux/errno.h>
#include <linux/signal.h>


int sigprocmask(int how, sigset_t *mask, sigset_t *omask)
{
        if (!mask)
                return __syscall_error(EINVAL);
        return __syscall_return(syscall(__NR_sigprocmask, (long)how, (long)mask, (long)omask));
}

