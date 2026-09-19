
#include <syscall.h>
#include <linux/errno.h>
#include <linux/types.h>


int ipc(uint call, int first, int second, int third, void *ptr, long fifth)
{
        syscall(__NR_ipc, (long)call, (long)first, (long)second);
        return syscall(__NR_ipc, (long)third, (long)ptr, (long)fifth);
}

