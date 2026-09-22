
#include <syscall.h>
#include <linux/errno.h>


int chdir(char *dir)
{
	if (!dir)
		return __syscall_error(EINVAL);
	return __syscall_return(syscall(__NR_chdir, (long)dir, 0, 0));
}
