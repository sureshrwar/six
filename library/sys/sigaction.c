
#include <syscall.h>
#include <linux/errno.h>
#include <linux/signal.h>

extern int sigreturn();

int sigaction(int signum, struct sigaction *new, struct sigaction *old)
{
	if (signum<0 || !new)
                return -EINVAL;
        syscall(__NR_sigaction, (long)signum, (long)new, (long)old);
        return syscall(__NR_sigaction, (long)sigreturn, 0, 0);
}

