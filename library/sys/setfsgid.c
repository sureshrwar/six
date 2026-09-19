
#include <syscall.h>
#include <linux/errno.h>
#include <linux/types.h>


int setfsgid(gid_t g)
{
        return syscall(__NR_setfsgid, (long)g, 0, 0);
}

