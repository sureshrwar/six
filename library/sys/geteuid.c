
#include <syscall.h>
#include <linux/errno.h>


int geteuid()
{
        return syscall(__NR_geteuid, 0, 0, 0);
}

