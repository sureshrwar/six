
#include <syscall.h>
#include <linux/errno.h>


int munlockall()
{
        return syscall(__NR_munlockall, 0, 0, 0);
}

