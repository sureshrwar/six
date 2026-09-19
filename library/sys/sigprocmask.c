
#include <syscall.h>
#include <linux/errno.h>
#include <linux/signal.h>


int sigprocmask(int how, sigset_t *mask, sigset_t *omask)
{
        if (!mask)
                return -EINVAL;
        return syscall(__NR_sigprocmask, (long)how, (long)mask, (long)omask);
}

