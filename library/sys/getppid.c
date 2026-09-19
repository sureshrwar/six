
#include <syscall.h>
#include <linux/errno.h>


int getppid()
{
        return syscall(__NR_getppid, 0, 0, 0);
}

