
#include <syscall.h>
#include <linux/errno.h>
#include <linux/types.h>


int munlock(unsigned long start, size_t len)
{
        return syscall(__NR_munlock, (long)start, (long)len, 0);
}

