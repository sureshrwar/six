
#include <syscall.h>
#include <linux/errno.h>


int sync()
{
        return __syscall_return(syscall(__NR_sync, 0, 0, 0));
}

