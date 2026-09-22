
#include <syscall.h>
#include <linux/errno.h>


int sgetmask()
{
        return __syscall_return(syscall(__NR_sgetmask, 0, 0, 0));
}

