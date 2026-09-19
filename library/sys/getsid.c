
#include <syscall.h>
#include <linux/errno.h>
#include <linux/types.h>


int getsid(pid_t p)
{
        return syscall(__NR_getsid, (long)p, 0, 0);
}

