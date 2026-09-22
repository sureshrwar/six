
#include <syscall.h>
#include <linux/errno.h>


int getpgrp()
{
        return __syscall_return(syscall(__NR_getpgrp, 0, 0, 0));
}

