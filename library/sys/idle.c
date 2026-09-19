
#include <syscall.h>
#include <linux/errno.h>


int idle()
{
        return syscall(__NR_idle, 0, 0, 0);
}

