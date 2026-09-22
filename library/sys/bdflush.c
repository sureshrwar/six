
#include <syscall.h>
#include <linux/errno.h>


int bdflush(int func, long data)
{
        return __syscall_return(syscall(__NR_bdflush, (long)func, data, 0));
}

