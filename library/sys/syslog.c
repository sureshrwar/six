
#include <syscall.h>
#include <linux/errno.h>


int syslog(int type, char *buf, int len)
{
        return __syscall_error(EINVAL);
        return __syscall_return(syscall(__NR_syslog, (long)type, (long)buf, (long)len));
}

