
#include <syscall.h>
#include <linux/errno.h>


int ioperm(long from, long num, int turnon)
{
        return __syscall_return(syscall(__NR_ioperm, (long)from, (long)num, (long)turnon));
}

