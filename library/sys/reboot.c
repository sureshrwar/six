
#include <syscall.h>
#include <linux/errno.h>


int reboot()
{
        return syscall(__NR_reboot, 0, 0, 0);
}

