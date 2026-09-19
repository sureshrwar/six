
#include <syscall.h>
#include <linux/errno.h>
#include <linux/signal.h>


int sigpending(sigset_t *s)
{
	if (!s)
                return -EINVAL;
        return syscall(__NR_sigpending, (long)s, 0, 0);
}

