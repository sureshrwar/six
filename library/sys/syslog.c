
#include <syscall.h>
#include <linux/errno.h>


int syslog(int type, char *buf, int len)
{
        return -EINVAL;
        return syscall(__NR_syslog, (long)type, (long)buf, (long)len);
}

