
#include <syscall.h>
#include <linux/errno.h>


int getgid()
{
        return syscall(__NR_getgid, 0, 0, 0);
}

