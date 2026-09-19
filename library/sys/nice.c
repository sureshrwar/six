
#include <syscall.h>
#include <linux/errno.h>


int nice(int incr)
{
        return syscall(__NR_nice, (long)incr, 0, 0);
}

