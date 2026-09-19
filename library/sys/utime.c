
#include <syscall.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/utime.h>


int utime(char *name, struct utimbuf *t)
{
	if (!name || !t)
                return -EINVAL;
        return syscall(__NR_utime, (long)name, (long)t, 0);
}

