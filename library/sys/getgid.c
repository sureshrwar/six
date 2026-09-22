
#include <syscall.h>
#include <linux/errno.h>


int getgid()
{
        return __syscall_return(syscall(__NR_getgid, 0, 0, 0));
}

