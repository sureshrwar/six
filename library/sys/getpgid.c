
#include <syscall.h>
#include <linux/errno.h>


int getpgid()
{
        return syscall(__NR_getpgid, 0, 0, 0);
}

