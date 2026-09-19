
#include <syscall.h>
#include <linux/errno.h>


int sgetmask()
{
        return syscall(__NR_sgetmask, 0, 0, 0);
}

