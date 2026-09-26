#include <syscall.h>
#include <errno.h>
#include <sys/ptrace.h>

extern int syscall5(int num, long one, long two, long three, long four, long five);

long ptrace(long request, long pid, long addr, long data)
{
	int ret;
	unsigned long peek_val = 0;

	if (request == PTRACE_PEEKTEXT ||
	    request == PTRACE_PEEKDATA ||
	    request == PTRACE_PEEKUSR) {
		ret = syscall5(__NR_ptrace, request, pid, addr, (long)&peek_val, 0);
		if (ret < 0) {
			errno = -ret;
			return -1L;
		}
		errno = 0;
		return (long)peek_val;
	}

	ret = syscall5(__NR_ptrace, request, pid, addr, data, 0);
	if (ret < 0) {
		errno = -ret;
		return -1L;
	}
	return (long)ret;
}
