
#include <syscall.h>
#include <linux/errno.h>


int vhangup()
{
        return __syscall_return(syscall(__NR_vhangup, 0, 0, 0));
}

