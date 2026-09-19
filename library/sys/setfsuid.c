
#include <syscall.h>
#include <linux/errno.h>
#include <linux/types.h>


int setfsuid(uid_t u)
{
        return syscall(__NR_setfsuid, (long)u, 0, 0);
}

