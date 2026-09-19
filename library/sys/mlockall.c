
#include <syscall.h>
#include <linux/errno.h>


int mlockall(int flags)
{
        return syscall(__NR_mlockall, (long)flags, 0, 0);
}

