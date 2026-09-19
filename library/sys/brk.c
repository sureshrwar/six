#include <syscall.h>
#include <linux/errno.h>

#define align(x)	(((x)+7) & ~7)

unsigned long __startbrk = 0, __endbrk = 0;

void brkinit()
{
	__startbrk = __endbrk = syscall(__NR_brk, (long)0, 0, 0);
}

unsigned long brk(unsigned long b)
{
	unsigned long old, new;

	/*
	 * if its the first time, then fill up __startbrk and __endbrk.
	 */
	if (!__startbrk && !__endbrk)
		brkinit();
	/*
	 * we have something in reserve, so no need for a system call.
	 */
	if (b > __startbrk && b < __endbrk)
	{
		__startbrk = b;
		return 0;
	}
	/*
	 * get the current brk value.
	 */
 	old = syscall(__NR_brk, (long)0, 0, 0);
	/*
	 * try setting the brk value to the new one.
	 */
 	new = syscall(__NR_brk, (long)b, 0, 0);

	/*
	 * if new > b, then it means we got more than we asked for. the "extra" that
	 * we got, will be our reserve. 
	 */
	if (new > b)	
	{
		__startbrk = b;
		__endbrk = new;
		return 0;
	}
	else if (new == b) /* we got what we asked for - no reserve nothing */
	{
		__startbrk = __endbrk = b;
		return 0;
	}
	return -1;
}

/*
 * increment the brk value by incr bytes.
 */
unsigned long sbrk(long incr)
{
	int ret;
	unsigned long temp;
	/*
	 * align the increment to a 8 byte boundary. if you ask for 1, you get 8.
	 * but if you ask for zero, you get zero. 
	 */
	if (incr)
		incr = align(incr);
	/*
	 * if its the first time, then fill up __startbrk and __endbrk.
	 */
	if (!__startbrk && !__endbrk)
		brkinit();

	/*
	 * take a backup of __startbrk; our return value ought to be the prior brk value,
	 * we need to remember.
	 */
	temp = __startbrk;
	/*
	 * we have something in reserve, so no need for a system call.
	 */
	if (__startbrk + incr <= __endbrk)
	{
		__startbrk += incr;
		return temp;
	}
	/*
	 * we dont have any (sufficient) reserves; so make a call to brk().
	 */
        ret = brk(__startbrk + incr);
	/*
	 * brk succeeded.
	 */
	if (!ret)
		return temp;
	else /* failed */
		return -1;
}

