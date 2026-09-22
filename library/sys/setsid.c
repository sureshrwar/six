
#include <syscall.h>
#include <linux/errno.h>


int setsid()
{
        return __syscall_return(syscall(__NR_setsid, 0, 0, 0));
}

