
#include <syscall.h>
#include <linux/errno.h>


int mmap(unsigned long addr, unsigned long len,
         unsigned long prot, unsigned long flags, unsigned long fd,
         unsigned long off)

{
        syscall(__NR_mmap, (long)addr, (long)len, (long)prot);
        return syscall(__NR_mmap, (long)flags, (long)fd, (long)off);
}

