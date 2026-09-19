
#include <syscall.h>
#include <linux/errno.h>


int munmap(unsigned long addr, unsigned long len)
{
        return syscall(__NR_munmap, (long)addr, (long)len, 0);
}

