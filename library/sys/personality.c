
#include <syscall.h>
#include <linux/errno.h>


int personality(unsigned long p)
{
        return syscall(__NR_personality, (long)p, 0, 0);
}

