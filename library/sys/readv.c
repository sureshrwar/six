
#include <syscall.h>
#include <linux/errno.h>
#include <linux/uio.h>


int readv(unsigned long fd, struct iovec *vec, long count)
{
	if (fd<0 || !vec || count<0)
                return __syscall_error(EINVAL);
        return __syscall_return(syscall(__NR_readv, (long)fd, (long)vec, count));
}

