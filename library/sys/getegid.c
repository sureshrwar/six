
#include <syscall.h>
#include <linux/errno.h>


int getegid()
{
        return syscall(__NR_getegid, 0, 0, 0);
}

