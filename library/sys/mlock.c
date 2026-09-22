
#include <syscall.h>
#include <linux/errno.h>
#include <linux/types.h>


int mlock(unsigned long start, size_t len)
{
        return __syscall_return(syscall(__NR_mlock, (long)start, (long)len, 0));
}

