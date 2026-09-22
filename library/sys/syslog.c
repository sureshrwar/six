
#include <syscall.h>
#include <linux/errno.h>


int klogctl(int type, char *buf, int len)
{
        if (len < 0)
                return __syscall_error(EINVAL);
        return __syscall_return(syscall(__NR_syslog, (long)type, (long)buf, (long)len));
}

int syslog(int type, char *buf, int len)
{
        if (type == 5 && (buf != (char *)0 || len != 0))
                return 0;
        return klogctl(type, buf, len);
}

