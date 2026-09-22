
#include <syscall.h>
#include <linux/errno.h>


int mremap(unsigned long addr, unsigned long olen, unsigned long nlen, unsigned long flags)
{
        syscall(__NR_mremap, (long)addr, (long)olen, (long)nlen);
        return __syscall_return(syscall(__NR_mremap, (long)flags, 0, 0));
}

