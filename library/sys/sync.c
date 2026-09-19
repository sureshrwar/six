
#include <syscall.h>
#include <linux/errno.h>


int sync()
{
        return syscall(__NR_sync, 0, 0, 0);
}

