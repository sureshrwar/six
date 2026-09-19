
#include <syscall.h>
#include <linux/errno.h>
#include <linux/types.h>


int msync(unsigned long start, size_t len, int flags)
{
        return syscall(__NR_msync, (long)start, (long)len, (long)flags);
}

