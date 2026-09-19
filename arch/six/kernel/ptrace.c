
#include <linux/sched.h>





asmlinkage int sys_ptrace(long request, long pid, long addr, long data)
{
	return ENOSYS;
}
