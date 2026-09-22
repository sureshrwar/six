
#include <syscall.h>
#include <linux/errno.h>

/*
 * Ok maybe this should go into libc
 */
int six_sleep(int secs)
{
	if (secs<0)
                return __syscall_error(EINVAL);
	alarm(secs);
	return pause();
}
