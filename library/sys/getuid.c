
#include <syscall.h>
#include <linux/errno.h>


unsigned long getuid()
{
        return syscall(__NR_getuid, 0, 0, 0);
}

