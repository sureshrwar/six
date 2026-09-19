
#include <syscall.h>
#include <errno.h>
#include <linux/types.h>
#include <linux/signal.h>

__sighandler_t signal(int num, __sighandler_t handler)
{
	int ret;
	struct sigaction sa, osa;

	if (num <= 0 || num > _NSIG || num == SIGKILL)
	{
		errno = EINVAL;
		return SIG_ERR;
	}

	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = handler;

	if ((ret = sigaction(num, &sa, &osa)) < 0)
	{
		errno = ret;
		return SIG_ERR;
	}

	return osa.sa_handler;
}

