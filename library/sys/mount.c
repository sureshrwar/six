
#include <syscall.h>
#include <linux/errno.h>


int mount(char *dev, char *dir, char *type, unsigned long new_flags, void *data)
{
	if (!dev || !dir || !type)
                return -EINVAL;
        syscall(__NR_mount, (long)dev, (long)dir, (long)type);
        return syscall(__NR_mount, (long)new_flags, (long)data, 0);
}

