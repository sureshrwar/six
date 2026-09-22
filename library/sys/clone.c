#include <errno.h>
#include <sched.h>
#include <unistd.h>

extern int syscall(int num, long one, long two, long three);

static void __clone_start_thread(int (*fn)(void *), void *arg)
{
	int ret = fn ? fn(arg) : 0;
	_exit(ret);
}

int clone(int (*fn)(void *), void *child_stack, int flags, void *arg)
{
	unsigned long *sp;
	int ret;

	if (!child_stack) {
		ret = syscall(120, (long)flags, 0, 0);
		if (ret < 0) {
			errno = -ret;
			return -1;
		}
		return ret;
	}
	if (!fn) {
		errno = EINVAL;
		return -1;
	}
	sp = (unsigned long *)((unsigned long)child_stack & ~15UL);
	sp -= 4;
	sp[0] = 0;
	sp[1] = (unsigned long)fn;
	sp[2] = (unsigned long)arg;
	sp[3] = 0;

	ret = syscall(120, (long)flags, (long)sp, (long)__clone_start_thread);
	if (ret < 0) {
		errno = -ret;
		return -1;
	}
	return ret;
}
