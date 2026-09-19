
#include <syscall.h>
#include <linux/errno.h>

/* only flush output buffers when necessary */
int (*_clean)(void) = 0;


void exit(int code)
{
        if (_clean)
		 _clean();
	syscall(__NR_exit, (long)code, 0, 0);
}

void _exit(int code)
{
	syscall(__NR_exit, (long)code, 0, 0);
}
