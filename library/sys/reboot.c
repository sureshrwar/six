
#include <syscall.h>
#include <linux/errno.h>


int reboot()
{
        return __syscall_return(syscall(__NR_reboot, 0, 0, 0));
}

