
#include <syscall.h>
#include <linux/errno.h>


int idle()
{
        return __syscall_return(syscall(__NR_idle, 0, 0, 0));
}

