
#include <syscall.h>
#include <linux/errno.h>


int mprotect(unsigned long addr, unsigned long len, unsigned long prot)
{
        return syscall(__NR_mprotect, (long)addr, (long)len, (long)prot);
}

