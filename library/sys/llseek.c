
#include <syscall.h>
#include <linux/errno.h>
#include <linux/types.h>


int llseek(int fd, unsigned long oh, unsigned long ol, loff_t *result, unsigned int origin)
{
        syscall(__NR__llseek, (long)fd, (long)oh, (long)ol);
        return syscall(__NR__llseek, (long)result, (long)origin, 0);
}

