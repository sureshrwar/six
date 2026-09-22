
#include <syscall.h>
#include <linux/errno.h>


int mount(char *dev, char *dir, char *type, unsigned long new_flags, void *data)
{
	if (!dev || !dir || !type)
                return __syscall_error(EINVAL);
        if ((new_flags & 0xffff0000UL) == 0)
                new_flags |= 0xC0ED0000UL;
        syscall(__NR_mount, (long)dev, (long)dir, (long)type);
        return __syscall_return(syscall(__NR_mount, (long)new_flags, (long)data, 0));
}

