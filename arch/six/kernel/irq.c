

/*
 *      linux/arch/i386/kernel/irq.c
 *
 *      Copyright (C) 1992 Linus Torvalds
 *
 * This file contains the code used by various IRQ handling routines:
 * asking for different IRQ's should be done through these routines
 * instead of just grabbing them. Thus setups with different IRQ numbers
 * shouldn't result in any weird surprises, and installing new handlers
 * should be easier.
 */

/*
 * IRQ's are in fact implemented a bit like signal handlers for the kernel.
 * Naturally it's not a 1:1 relation, but there are similarities.
 */

#include "host.h"
#include <solaris.h>

#include <linux/ptrace.h>
#include <asm/sixcall.h>
#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/malloc.h>
#include <linux/random.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/smp.h>

#define CR0_NE 32


/*
 * What happens in the case of say, timer_interrupt :
 * you use BUILD_IRQ(chip, nr, mask) to declare the function 
 * IRQ0_interrupt(). This is put into interrupt[0]. Interrupt[0]
 * is registered using set_intr_gate() so that when the
 * timer interrupt happens, this function is called. What it 
 * does is :
 *	increment intr_count
 *	call do_IRQ()
 * 	decrement intr_count
 *	call ret_from_sys_call
 */ 


#include <six_proc.h>

unsigned long intr_count = 0;
unsigned long kernel_counter = 0;
extern void winch_setsize(int, int, int);
extern int need_resched;
void schedule(void);
extern void reset_sun_tty();


/*
 * The syscall argument channel.  See include/asm-six/sixcall.h for why
 * this is a struct in memory rather than the MMX registers it used to be.
 */
#if (__i386__)
struct six_call_regs six_call;
#endif

int syscall(int num, long one, long two, long three)
{
#if (__i386__)
        /*
         * Used to be:
         *      mm0 = (one << 32) | num
         *      mm2 = (two << 32) | three
         * with the halves read back out of the FPU image embedded in the
         * Solaris ucontext.  Linux clears MMX before signal handlers run,
         * so the values are now simply left in memory.  The g4/g5
         * inversion below is inherited from the old mm2 packing and is
         * what grab_args() expects.
         */
        six_call.g2 = num;
        six_call.g3 = one;
        six_call.g4 = three;
        six_call.g5 = two;

        INT_SYSCALL;

        return (long)six_call.g2;
#else
        long ret;
        __asm__("mov %1, %%g2"   :  "=r" (ret)  : "r" (num));
        __asm__("mov %1, %%g3"   :  "=r" (ret)  : "r" (one));
        __asm__("mov %1, %%g4"   :  "=r" (ret)  : "r" (two));
        __asm__("mov %1, %%g5"   :  "=r" (ret)  : "r" (three));

	INT_SYSCALL;

        __asm__("mov %%g2, %0" : "=r" (ret));

        return ret;

#endif
}

/*
 * The system call mechanism:
 * first calls getpid and gets our own pid. Then calls kill using the
 * pid and SIGLWP as arguments so that we hit ourselves with a SIGLWP
 * causing the system call handler system_handler to be invoked.
 */

int0x80()
{
#if (__i386__)
	/*
	 * Raise the SIX syscall trap by signalling ourselves.
	 *
	 * Solaris entered the kernel through its call gate:
	 *
	 *      Pushl $0x21             ! SIGLWP
	 *      movl  $0x14,%eax        ! getpid
	 *      lcall $0x7,$0x0         ! Arguments on the stack
	 *
	 * Linux/i386 instead uses "int $0x80" with the arguments in
	 * registers (eax = number, ebx/ecx/edx = args).  Conveniently the
	 * call numbers themselves are identical -- both Solaris and Linux
	 * inherit them from System V -- so only the gate changes:
	 *
	 *      getpid = 20 = 0x14      kill = 37 = 0x25
	 *
	 * This is deliberately raw assembly rather than a call to the host
	 * libc, so that it is unambiguously a *host* system call and cannot
	 * be confused with SIX's own emulated syscall layer.
	 *
	 * The "b" constraint is safe because the kernel is built -fno-pic,
	 * so %ebx is not reserved for the GOT pointer.
	 */
	int pid;

	__asm__ __volatile__ ("int $0x80"
			      : "=a" (pid)
			      : "0"  (20)               /* __NR_getpid */
			      : "memory");

	__asm__ __volatile__ ("int $0x80"
			      :
			      : "a" (37),               /* __NR_kill    */
				"b" (pid),
				"c" (SIX_TRAPSIG)
			      : "memory");
#endif
}


void grab_args(struct pt_regs *regs, long *one, long *two, long *three)
{
                if(one)
                        *one = regs->g3;  /* mm0 second part */
#if (__i386__)
                if(two)
                        *two = regs->g5; /* mm2 second part */
                if(three)
                        *three = regs->g4; /* mm2 first part */
#else
                if(two)
                        *two = regs->g4;
                if(three)
                        *three = regs->g5;
#endif
}

void put_ret(struct pt_regs *u, long retval)
{       
        u->g2 = retval;
}

void six_clone(struct pt_regs *u)
{
	int ret, cloneflags;
        grab_args(u, (long *)&cloneflags, 0, 0);
	ret = do_fork(cloneflags, 0, u);
	put_ret(u, (long)ret);
}

void six_execve(struct pt_regs *u)
{
	char **argv, **envp, *filename;
        grab_args(u, (long *)&filename, (long *)&argv, (long *)&envp);
	do_execve(filename, argv, envp, u);
}

void six_brk(struct pt_regs *u)
{
	unsigned long brk, ret;
	grab_args(u, (long *)&brk, 0, 0);
	ret = sys_brk(brk);
	put_ret(u, (long)ret);
}

void six_read(struct pt_regs *u)
{
        char *buf;
        int len, fd, ret;
        grab_args(u, (long *)&fd, (long *)&buf, (long *)&len);
        ret = sys_read(fd, buf, len);
        put_ret(u, (long)ret);
}

void six_signal(struct pt_regs *u)
{
	int num, ret;
	long hand;
	grab_args(u, (long)&num, (long)&hand, 0);
	ret = sys_signal(num, hand);
	put_ret(u, (long)ret);
}

void six_write(struct pt_regs *u)
{
        char *buf;
        int fd, len, ret;

        grab_args(u, (long *)&fd, (long *)&buf, (long *)&len);
        ret = sys_write(fd, buf, len);
        put_ret(u, (long)ret);
}


void six_open(struct pt_regs *u)
{
        char *name;
        long flags, mode;
        int fd;
        grab_args(u, (long *)&name, &flags, &mode);
        fd = sys_open(name, flags, mode);
        put_ret(u, (long)fd);
}

void six_alarm(struct pt_regs *u)
{
         long secs;
         grab_args(u, &secs, 0, 0);
         sys_alarm(secs);
}

void six_time(struct pt_regs *u)
{
         long *secs, ret;
         grab_args(u, &secs, 0, 0);
	 ret = sys_time(secs);	
	 put_ret(u, ret);
}

void six_getpid(struct pt_regs *u)
{
	int pid = sys_getpid();
        put_ret(u, (long)pid);
}

void six_fork(struct pt_regs *u)
{
	int ret;
	ret = do_fork(SIGCHLD, 0, u);
        put_ret(u, (long)ret);
}

void six_kill(struct pt_regs *u)
{
	int pid, sig, ret;
        grab_args(u, (long *)&pid, (long *)&sig, 0);
	ret = sys_kill(pid, sig);
	put_ret(u, (long)ret);
}

void six_getdents(struct pt_regs *u)
{
	int fd, count, ret;
	void *buf;
        grab_args(u, (long *)&fd, (long *)&buf, (long *)&count);
	ret = sys_getdents(fd, buf, count);
	put_ret(u, (long)ret);	
}

void six_waitpid(struct pt_regs *u)
{
	int pid, options, ret;
	unsigned int *staddr;
        grab_args(u, (long *)&pid, (long *)&staddr, (long *)&options);
	ret = sys_waitpid(pid, staddr, options);
	put_ret(u, (long)ret);
}


void six_fstat(struct pt_regs *u)
{
	int ret, fd;
	struct old_stat *s;
        grab_args(u, (long *)&fd, (long *)&s, 0);
	ret = sys_fstat(fd, s);
	put_ret(u, (long)ret);
}

void six_access(struct pt_regs *u)
{
	char *filename;
	int mode, ret;
        grab_args(u, (long *)&filename, (long *)&mode, 0);
	ret = sys_access(filename, mode);
	put_ret(u, (long)ret);
}

void six_chdir(struct pt_regs *u)
{
	int ret;
	char *dir;
	grab_args(u, (long *)&dir, 0, 0);
	ret = sys_chdir(dir);
	put_ret(u, (long)ret);
}

void six_ps(struct pt_regs *u)
{
	struct six_proc *sp;
	int ret;	
	grab_args(u, (long *)&sp, 0, 0);
	ret = sys_sixps(sp);
	put_ret(u, (long)ret);	
}

void six_ioctl(struct pt_regs *u)
{
	int fd, cmd, arg, ret;
	grab_args(u, (long *)&fd, (long *)&cmd, (long *)&arg);
	ret = sys_ioctl(fd, cmd, arg);
	put_ret(u, (long)ret);	
}

void six_getpagesize(struct pt_regs *u)
{
	put_ret(u, (long)PAGE_SIZE);
}

void six_close(struct pt_regs *u)
{
	int fd, ret;
	grab_args(u, (long *)&fd, 0, 0);
	ret = sys_close(fd);
	put_ret(u, (long)ret);
}

void six_creat(struct pt_regs *u)
{
	char *pathname;
	int mode, fd;
	grab_args(u, (long *)&pathname, (int *)&mode, 0);
	fd = sys_creat(pathname, mode);
	put_ret(u, (long)fd);
}

void six_link(struct pt_regs *u)
{
	char *old, *new;
	int ret;
	grab_args(u, (long *)&old, (long *)&new, 0);
	ret = sys_link(old, new);
	put_ret(u, (long)ret);
}

void six_unlink(struct pt_regs *u)
{
	char *name;
	int ret;
	grab_args(u, (long *)&name, 0, 0);
	ret = sys_unlink(name);
	put_ret(u, (long)ret);
}

void six_mknod(struct pt_regs *u)
{
	char *name;
	int mode, ret;
	long dev;
	grab_args(u, (long *)&name, (int *)&mode, (long *)&dev);
	ret = sys_mknod(name, mode, dev);
	put_ret(u, (long)ret);
}

void six_chmod(struct pt_regs *u)
{
	char *name;
	int mode, ret;
	grab_args(u, (long *)&name, (int *)&mode, 0);
	ret = sys_chmod(name, mode);
	put_ret(u, (long)ret);
}

void six_setup(struct pt_regs *u)
{
	int ret;
	ret = sys_setup();
	put_ret(u, (long)ret);
}

void six_chown(struct pt_regs *u)
{
	char *name;
	int user, group, ret;
	grab_args(u, (long *)&name, (long *)&user, (long *)&group);
	ret = sys_chown(name, user, group);
	put_ret(u, (long)ret);
}

void six_break(struct pt_regs *u)
{
	int ret = sys_break();
	put_ret(u, (long)ret);
}

void six_lseek(struct pt_regs *u)
{
	int fd, origin, ret;
	long offset;	
	grab_args(u, (long *)&fd, (long *)&offset, (long *)&origin);
	ret = sys_lseek(fd, offset, origin);
	put_ret(u, (long)ret);
}

void six_setuid(struct pt_regs *u)
{
	int uid, ret;
	grab_args(u, (long *)&uid, 0, 0);
	ret = sys_setuid(uid);
	put_ret(u, (long)ret);
}

void six_getuid(struct pt_regs *u)
{
	int ret;
	ret = sys_getuid();
	put_ret(u, (long)ret);
}

void six_stime(struct pt_regs *u)
{
	int *t, ret;
	grab_args(u, (long *)&t, 0, 0);
	ret = sys_stime(t);
	put_ret(u, (long)ret);
}

void six_utime(struct pt_regs *u)
{
	int ret;
	char *filename;
	struct utimbuf *t;
	grab_args(u, (long *)&filename, (long *)&t, 0);
	ret = sys_utime(filename, t);
	put_ret(u, (long)ret);
}

void six_pause(struct pt_regs *u)
{
	int ret;
	ret = sys_pause();
	put_ret(u, (long)ret);
}

void six_exit(struct pt_regs *u)
{
	int status, ret;
	grab_args(u, (long *)&status, 0, 0);
	ret = sys_exit(status);
	put_ret(u, (long)ret);
}

void six_nice(struct pt_regs *u)
{
	int incr, ret;
	grab_args(u, (long *)&incr, 0, 0);
	ret = sys_nice(incr);
	put_ret(u, (long)ret);
}

void six_sync(struct pt_regs *u)
{
	int ret;
	ret = sys_sync();
	put_ret(u, (long)ret);
}

void six_rename(struct pt_regs *u)
{
	char *old, *new;
	int ret;
	grab_args(u, (long *)&old, (long *)&new, 0);
	ret = sys_rename(old, new);
	put_ret(u, (long)ret);
}

void six_mkdir(struct pt_regs *u)
{
	char *filename;
	int ret, mode;
	grab_args(u, (long *)&filename, (long *)&mode, 0);
	ret = sys_mkdir(filename, mode);
	put_ret(u, (long)ret);
}

void six_rmdir(struct pt_regs *u)
{
	char *filename;
	int ret;
	grab_args(u, (long *)&filename, 0, 0);
	ret = sys_rmdir(filename);
	put_ret(u, (long)ret);
}

void six_dup(struct pt_regs *u)
{
	int fd, ret;
	grab_args(u, (long *)&fd, 0, 0);
	ret = sys_dup(fd);
	put_ret(u, (long)ret);
}

void six_pipe(struct pt_regs *u)
{
	int *fd, ret;
	grab_args(u, (long *)&fd, 0, 0);
	ret = sys_pipe(fd);
	put_ret(u, (long)ret);
}

void six_times(struct pt_regs *u)
{
	struct tms *t;
	int ret;
	grab_args(u, (long *)&t, 0, 0);
	ret = sys_times(t);
	put_ret(u, (long)ret);
}

void six_setgid(struct pt_regs *u)
{
	int gid, ret;
	grab_args(u, (long *)&gid, 0, 0);
	ret = sys_setgid(gid);
	put_ret(u, (long)ret);
}

void six_getgid(struct pt_regs *u)
{
	int ret;
	ret = sys_getgid();
	put_ret(u, (long)ret);
}

void six_geteuid(struct pt_regs *u)
{
	int ret;
	ret = sys_geteuid();
	put_ret(u, (long)ret);
}

void six_getegid(struct pt_regs *u)
{
	int ret;
	ret = sys_getegid();
	put_ret(u, (long)ret);
}

void six_acct(struct pt_regs *u)
{
	char *name;
	int ret;
	grab_args(u, (long *)&name, 0, 0);
	ret = sys_acct(name);
	put_ret(u, (long)ret);
}

void six_fcntl(struct pt_regs *u)
{
	int fd, cmd, ret;
	long arg;
	grab_args(u, (long *)&fd, (long *)&cmd, (long *)&arg);
	ret = sys_fcntl(fd, cmd, arg);
	put_ret(u, (long)ret);
}

void six_setpgid(struct pt_regs *u)
{
	int pid, pgid, ret;
	grab_args(u, (long *)&pid, (long *)&pgid, 0);
	ret = sys_setpgid(pid, pgid);
	put_ret(u, (long)ret);
}

void six_umask(struct pt_regs *u)
{
	int mask, ret;
	grab_args(u, (long *)&mask, 0, 0);
	ret = sys_umask(mask);
	put_ret(u, (long)ret);
}

void six_chroot(struct pt_regs *u)
{
	char *name;
	int ret;
	grab_args(u, (long *)&name, 0, 0);
	ret = sys_chroot(name);
	put_ret(u, (long)ret);
}

void six_ustat(struct pt_regs *u)
{
	long dev;
	struct ustat *ubuf;
	int ret;
	grab_args(u, (long *)&dev, (long *)&ubuf, 0);
	ret = sys_ustat(dev, ubuf);
	put_ret(u, (long)ret);
}

void six_dup2(struct pt_regs *u)
{
	int ofd, nfd, ret;
	grab_args(u, (long *)&ofd, (long *)&nfd, 0);
	ret = sys_dup2(ofd, nfd);
	put_ret(u, (long)ret);
}

void six_getppid(struct pt_regs *u)
{
	int ret;
	ret = sys_getppid();
	put_ret(u, (long)ret);
}

void six_getpgrp(struct pt_regs *u)
{
	int ret;
	ret = sys_getpgrp();
	put_ret(u, (long)ret);
}

void six_setsid(struct pt_regs *u)
{
	int ret;
	ret = sys_setsid();
	put_ret(u, (long)ret);
}

void six_sigaction(struct pt_regs *u)
{
	int signum, ret;
	struct sigaction *new, *old;
	unsigned long handler;
	if (current->one)
	{
		grab_args(u, (long *)&handler, 0, 0);
		signum = (int)current->one;
		new = (struct sigaction *)current->two;
		old = (struct sigaction *)current->three;
		ret = sys_sigaction(signum, new, old, handler);
		current->one = current->two = current->three = 0;
	}
	else
	{
		grab_args(u, (long *)&signum, (long *)&new, (long *)&old);
		current->one = (long)signum;
		current->two = (long)new;
		current->three = (long)old;
		return;
	}
	put_ret(u, (long)ret);
}

void six_sgetmask(struct pt_regs *u)
{
	int ret;
	ret = sys_sgetmask();
	put_ret(u, (long)ret);
}

void six_ssetmask(struct pt_regs *u)
{
	int mask, ret;
	grab_args(u, (long *)&mask, 0, 0);
	ret = sys_ssetmask(mask);
	put_ret(u, (long)ret);
}

void six_setreuid(struct pt_regs *u)
{
	int ruid, euid, ret;
	grab_args(u, (long *)&ruid, (long *)&euid, 0);
	ret = sys_setreuid(ruid, euid);
	put_ret(u, (long)ret);
}

void six_setregid(struct pt_regs *u)
{
	int rgid, egid, ret;
	grab_args(u, (long *)&rgid, (long *)&egid, 0);
	ret = sys_setregid(rgid, egid);
	put_ret(u, (long)ret);
}

void six_sigsuspend(struct pt_regs *u)
{
	unsigned long *set;
	int ret;
	grab_args(u, (long *)&set, 0, 0);
	ret = sys_sigsuspend(u, set);
	put_ret(u, (long)ret);
}

void six_sigpending(struct pt_regs *u)
{
	sigset_t *s;
	int ret;
	grab_args(u, (long *)&s, 0, 0);
	ret = sys_sigpending(s);
	put_ret(u, (long)ret);
}

void six_sethostname(struct pt_regs *u)
{
	char *name;
	int len, ret;
	grab_args(u, (long *)&name, (long *)&len, 0);
	ret = sys_sethostname(name, len);
	put_ret(u, (long)ret);
}

void six_setrlimit(struct pt_regs *u)
{
	unsigned int resource;
	int ret;
	struct rlimit *r;
	grab_args(u, (long *)&resource, (long *)&r, 0);
	ret = sys_setrlimit(resource, r);
	put_ret(u, (long)ret);
}

void six_getrlimit(struct pt_regs *u)
{
	unsigned int resource;
	int ret;
	struct rlimit *r;
	grab_args(u, (long *)&resource, (long *)&r, 0);
	ret = sys_getrlimit(resource, r);
	put_ret(u, (long)ret);
}

void six_getrusage(struct pt_regs *u)
{
	unsigned int who;
	int ret;
	struct rusage *r;
	grab_args(u, (long *)&who, (long *)&r, 0);
	ret = sys_getrusage(who, r);
	put_ret(u, (long)ret);
}

void six_gettimeofday(struct pt_regs *u)
{
	struct timeval *tv;
	struct timezone *tz;
	int ret;
	grab_args(u, (long *)&tv, (long *)&tz, 0);
	ret = sys_gettimeofday(tv, tz);
	put_ret(u, (long)ret);
}

void six_settimeofday(struct pt_regs *u)
{
	struct timeval *tv;
	struct timezone *tz;
	int ret;
	grab_args(u, (long *)&tv, (long *)&tz, 0);
	ret = sys_settimeofday(tv, tz);
	put_ret(u, (long)ret);
}


void six_getgroups(struct pt_regs *u)
{
	int gidsetsize, *glist, ret;
	grab_args(u, (long *)&gidsetsize, (long *)&glist, 0);
	ret = sys_getgroups(gidsetsize, glist);
	put_ret(u, (long)ret);
}

void six_setgroups(struct pt_regs *u)
{
	int gidsetsize, *glist, ret;
	grab_args(u, (long *)&gidsetsize, (long *)&glist, 0);
	ret = sys_setgroups(gidsetsize, glist);
	put_ret(u, (long)ret);
}

void six_symlink(struct pt_regs *u)
{
	char *new, *old;
	int ret;
	grab_args(u, (long *)&new, (long *)&old, 0);
	ret = sys_symlink(old, new);
	put_ret(u, (long)ret);
}

void six_readlink(struct pt_regs *u)
{
	char *name, *buf;
	int size, ret;
	grab_args(u, (long *)&name, (long *)&buf, (long *)&size);
	ret = sys_readlink(name, buf, size);
	put_ret(u, (long)ret);
}

void six_uselib(struct pt_regs *u)
{
	char *name;
	int ret;
	grab_args(u, (long *)&name, 0, 0);
	ret = sys_uselib(name);
	put_ret(u, (long)ret);
}

void six_select(struct pt_regs *u)
{
	int n, ret = 0;
	fd_set *inp, *outp, *exp;
	struct timeval *tvp;
	if (current->one)
	{
		grab_args(u, (long *)&exp, (long *)&tvp, 0);
		n = (int)current->one;
		inp = (fd_set *)current->two;
		outp = (fd_set *)current->three;
		ret = sys_select(n, inp, outp, exp, tvp);
		current->one = current->two = current->three = 0;
	}
	else
	{
		grab_args(u, (long *)&n, (long *)&inp, (long *)&outp);
		current->one = (long)n;
		current->two = (long)inp;
		current->three = (long)outp;
		return;
	}
	put_ret(u, (long)ret);
}

void six_newselect(struct pt_regs *u)
{
	int n = (int)u->g3;
	fd_set *inp = (fd_set *)u->g5;
	fd_set *outp = (fd_set *)u->g4;
	fd_set *exp = (fd_set *)u->g7;
	struct timeval *tvp = (struct timeval *)u->g8;
	int ret = sys_select(n, inp, outp, exp, tvp);
	put_ret(u, (long)ret);
}

void six_umount(struct pt_regs *u)
{
	int ret;
	char *name;
	grab_args(u, (long *)&name, 0, 0);
	ret = sys_umount(name);	
	put_ret(u, (long)ret);
}

void six_mount(struct pt_regs *u)
{
	char *devname, *dirname, *type;
	unsigned long flags;
	void *data;
	int ret = 0;
	if (current->one)
	{
		grab_args(u, (long *)&flags, (long *)&data, 0);
		devname = (char *)current->one;
		dirname = (char *)current->two;
		type = (char *)current->three;
		ret = sys_mount(devname, dirname, type, flags, data);
		current->one = current->two = current->three = 0;
	}
	else
	{
		grab_args(u, (long *)&devname, (long *)&dirname, (long *)&type);
		current->one = (long)devname;
		current->two = (long)dirname;
		current->three = (long)type;
		return;
	}
	put_ret(u, (long)ret);
}

void six_swapon(struct pt_regs *u)
{
	char *name;
	int flags, ret;
	grab_args(u, (long *)&name, (long *)&flags, 0);
	ret = sys_swapon(name, flags);
	put_ret(u, (long)ret);
}

void six_swapoff(struct pt_regs *u)
{
	char *name;
	int ret;
	grab_args(u, (long *)&name, 0, 0);
	ret = sys_swapoff(name);
	put_ret(u, (long)ret);
}

void six_reboot(struct pt_regs *u)
{
	unsigned long magic = 0, magic2 = 0, flag = 0;
	int ret;
	grab_args(u, (long *)&magic, (long *)&magic2, (long *)&flag);
	if (magic == 0xfee1deadUL && flag == 0xdead000dUL) {
		for (;;) {
			current->state = TASK_UNINTERRUPTIBLE;
			schedule();
		}
	}
	if (magic == 0xfee1deadUL && (flag & 0xffff0000UL) == 0xdead0000UL) {
		extern unsigned long panic_print;
		if (flag & 0x0100UL)
			panic_print = flag & 0x7fUL;
		if (magic2 >= 0x03000000UL && magic2 < TASK_SIZE &&
		    *(const char *)magic2 != '\0')
			panic("%s", (const char *)magic2);
		panic("SysRq : Trigger a crashdump");
	}
	ret = sys_reboot(0xfee1dead, 672274793, 0xCDEF0123);
	put_ret(u, (long)ret);
}

void enosyscall(struct pt_regs *u)
{
	put_ret(u, (long)ENOSYS);
}

void six_readdir(struct pt_regs *u)
{
	int fd, count, ret;
	void *dirent;
	grab_args(u, (long *)&fd, (long *)&dirent, (long *)&count);
	ret = old_readdir(fd, dirent, count);
	put_ret(u, (long)ret);
}

void six_mmap(struct pt_regs *u)
{
	unsigned long addr, len, prot, flags, off;
	int fd, ret = 0;
	if (current->one)
	{
		grab_args(u, (long *)&flags, (long *)&fd, (long *)&off);
		addr = (unsigned long)current->one;
		len = (unsigned long)current->two;
		prot = (unsigned long)current->three;
		ret = sys_mmap(addr, len, prot, flags, fd, off);
		current->one = current->two = current->three = 0;
	}
	else
	{
		grab_args(u, (long *)&addr, (long *)&len, (long *)&prot);
		current->one = (long)addr;
		current->two = (long)len;
		current->three = (long)prot;
		return;
	}
	put_ret(u, (long)ret);
}

void six_munmap(struct pt_regs *u)
{
	unsigned long addr;
	size_t len;
	int ret;
	grab_args(u, (long *)&addr, (long *)&len, 0);
	ret = do_munmap(addr, len);
	put_ret(u, (long)ret);
}

void six_truncate(struct pt_regs *u)
{
	char *path;
	unsigned long len;
	int ret;
	grab_args(u, (long *)&path, (long *)&len, 0);
	ret = sys_truncate(path, len);
	put_ret(u, (long)ret);
}

void six_ftruncate(struct pt_regs *u)
{
	int fd, ret;
	unsigned long len;
	grab_args(u, (long *)&fd, (long *)&len, 0);
	ret = sys_ftruncate(fd, len);
	put_ret(u, (long)ret);
}

void six_fchmod(struct pt_regs *u)
{
	int fd, ret;
	mode_t mode;
	grab_args(u, (long *)&fd, (long *)&mode, 0);
	ret = sys_fchmod(fd, mode);
	put_ret(u, (long)ret);
}

void six_fchown(struct pt_regs *u)
{
	int fd, ret;
	uid_t uid;
	gid_t g;
	grab_args(u, (long *)&fd, (long *)&uid, (long *)&g);
	ret = sys_fchown(fd, uid, g);
	put_ret(u, (long)ret);
}

void six_getpriority(struct pt_regs *u)
{
	int which, who, ret;
	grab_args(u, (long *)&which, (long *)&who, 0);
	ret = sys_getpriority(which, who);
	put_ret(u, (long)ret);
}

void six_setpriority(struct pt_regs *u)
{
	int which, who, niceval, ret;
	grab_args(u, (long *)&which, (long *)&who, (long *)&niceval);
	ret = sys_setpriority(which, who, niceval);
	put_ret(u, (long)ret);
}

void six_statfs(struct pt_regs *u)
{
	char *path;
	struct statfs *s;
	int ret;
	grab_args(u, (long *)&path, (long *)&s, 0);
	ret = sys_statfs(path, s);
	put_ret(u, (long)ret);
}

void six_fstatfs(struct pt_regs *u)
{
	int fd, ret;
	struct statfs *s;
	grab_args(u, (long *)&fd, (long *)&s, 0);
	ret = sys_statfs(fd, s);
	put_ret(u, (long)ret);
}

void six_ioperm(struct pt_regs *u)
{
	unsigned long from, num;
	int turnon, ret;
	grab_args(u, (long *)&from, (long *)&num, (long *)&turnon);
	ret = sys_ioperm(from, num, turnon);
	put_ret(u, (long)ret);
}

void six_socketcall(struct pt_regs *u)
{
	int call, ret;
	unsigned long *args;
	grab_args(u, (long *)&call, (long *)&args, 0);
	ret = sys_socketcall(call, args);
	put_ret(u, (long)ret);
}

void six_syslog(struct pt_regs *u)
{
	int type, len, ret;
	char *buf;
	grab_args(u, (long *)&type, (long *)&buf, (long *)&len);
	ret = sys_syslog(type, buf, len);
	put_ret(u, (long)ret);
}

void six_setitimer(struct pt_regs *u)
{
	int which, ret;
	struct itimerval *val, *oval;
	grab_args(u, (long *)&which, (long *)&val, (long *)&oval);
	ret = sys_setitimer(which, val, oval);
	put_ret(u, (long)ret);
}

void six_getitimer(struct pt_regs *u)
{
	int which, ret;
	struct itimerval *val;
	grab_args(u, (long *)&which, (long *)&val, 0);
	ret = sys_getitimer(which, val);
	put_ret(u, (long)ret);
}

void six_stat(struct pt_regs *u)
{
	char *filename;
	struct old_stat *s;
	int ret;
	grab_args(u, (long *)&filename, (long *)&s, 0);
	ret = sys_stat(filename, s);
	put_ret(u, (long)ret);
}

void six_lstat(struct pt_regs *u)
{
	char *filename;
	struct old_stat *s;
	int ret;
	grab_args(u, (long *)&filename, (long *)&s, 0);
	ret = sys_lstat(filename, s);
	put_ret(u, (long)ret);
}

void six_olduname(struct pt_regs *u)
{
	struct oldold_utsname *name;
	int ret;
	grab_args(u, (long *)&name, 0, 0);
	ret = sys_olduname(name);
	put_ret(u, (long)ret);
}

void six_vhangup(struct pt_regs *u)
{
	int ret;
	ret = sys_vhangup();
	put_ret(u, (long)ret);
}

void six_idle(struct pt_regs *u)
{
	int ret;
	ret = sys_idle();
	put_ret(u, (long)ret);
}

void six_wait4(struct pt_regs *u)
{
	pid_t pid;
	unsigned int *stat_addr;
	int options, ret;
	struct rusage *ru;

	if (current->one)
	{
		grab_args(u, (long *)&ru, 0, 0);
		pid = (pid_t)current->one;
		stat_addr = (unsigned int *)current->two;
		options = (int)current->three;
		ret = sys_wait4(pid, stat_addr, options, ru);
		current->one = current->two = current->three = 0;
	}
	else
	{
		grab_args(u, (long *)&pid, (long *)&stat_addr, (long *)&options);
		current->one = (long)pid;
		current->two = (long)stat_addr;
		current->three = (long)options;
		return;
	}
	put_ret(u, (long)ret);
}

void six_sysinfo(struct pt_regs *u)
{
	struct sysinfo *info;
	int ret;
	grab_args(u, (long *)&info, 0, 0);
	ret = sys_sysinfo(info);
	put_ret(u, (long)ret);
}

void six_ipc(struct pt_regs *u)
{
	uint call;
	int first, second, third, ret;
	void *ptr;
	long fifth;

	if (current->one)
	{
		grab_args(u, (long *)&third, (long *)&ptr, (long *)&fifth);
		call = (pid_t)current->one;
		first = (unsigned int *)current->two;
		second = (int)current->three;
		ret = sys_ipc(call, first, second, third, ptr, fifth);
		current->one = current->two = current->three = 0;
	}
	else
	{
		grab_args(u, (long *)&call, (long *)&first, (long *)&second);
		current->one = (long)call;
		current->two = (long)first;
		current->three = (long)second;
		return;
	}
	put_ret(u, (long)ret);
}

void six_fsync(struct pt_regs *u)
{
	int fd, ret;
	grab_args(u, (long *)&fd, 0, 0);
	ret = sys_fsync(fd);
	put_ret(u, (long)ret);
}

void six_sigreturn(struct pt_regs *u)
{
	long sc;
	int ret;
	grab_args(u, (long *)&sc, 0, 0);
	/*
	 * U will be modified by sigreturn to bring the
	 * process back to whatever it was doing at the
	 * point of signal arrival.
	 */
	sys_sigreturn(sc, u);
	/*
	 * Don't worry about return values here. By now sigreturn
	 * would have modified the context structure, and so when
	 * we go back to sun_handler() and finally onto the
	 * setcontext() that brings us to userland, the process
	 * finds itself at an earlier point of time - at the
	 * point of signal arrival.
	 */
}

void six_setdomainname(struct pt_regs *u)
{
	char *name;
	int len, ret;
	grab_args(u, (long *)&name, (long *)&len, 0);
	ret = sys_setdomainname(name, len);
	put_ret(u, (long)ret);
}

void six_uname(struct pt_regs *u)
{
	struct new_utsname *name;
	int ret;
	grab_args(u, (long *)&name, 0, 0);
	ret = sys_newuname(name);
	put_ret(u, (long)ret);
}

void six_mprotect(struct pt_regs *u)
{
	unsigned long start, prot;
	size_t len;
	int ret;
	grab_args(u, (long *)&start, (long *)&len, (long *)&prot);
	ret = sys_mprotect(start, len, prot);
	put_ret(u, (long)ret);
}

void six_sigprocmask(struct pt_regs *u)
{
	int how, ret;
	sigset_t *mask, *omask;
	grab_args(u, (long *)&how, (long *)&mask, (long *)&omask);
	ret = sys_sigprocmask(how, mask, omask);
	put_ret(u, (long)ret);
}

void six_getpgid(struct pt_regs *u)
{
	int ret;
	ret = sys_getpgid();
	put_ret(u, (long)ret);
}

void six_fchdir(struct pt_regs *u)
{
	int fd, ret;
	grab_args(u, (long *)&fd, 0, 0);
	ret = sys_fchdir(fd);
	put_ret(u, (long)ret);
}

void six_bdflush(struct pt_regs *u)
{
	int func, ret;
	long data;
	grab_args(u, (long *)&func, (long *)&data, 0);
	ret = sys_bdflush(func, data);
	put_ret(u, (long)ret);
}

void six_personality(struct pt_regs *u)
{
	unsigned long p;
	int ret;
	grab_args(u, (long *)&p, 0, 0);
	ret = sys_personality(p);
	put_ret(u, (long)ret);
}

void six_adjtimex(struct pt_regs *u)
{
	struct timex *t;
	int ret;
	grab_args(u, (long *)&t, 0, 0);
	ret = sys_adjtimex(t);
	put_ret(u, (long)ret);
}

void six_setfsuid(struct pt_regs *u)
{
	uid_t uid;
	int ret;
	grab_args(u, (long *)&uid, 0, 0);
	ret = sys_setfsuid(uid);
	put_ret(u, (long)ret);
}

void six_setfsgid(struct pt_regs *u)
{
	gid_t g;
	int ret;
	grab_args(u, (long *)&g, 0, 0);
	ret = sys_setfsuid(g);
	put_ret(u, (long)ret);
}

void six_llseek(struct pt_regs *u)
{

	int fd, ret;
	unsigned long oh, ol;
	loff_t *result;
	unsigned int origin;

	if (current->one)
	{
		grab_args(u, (long *)&result, (long *)&origin, 0);
		fd = (pid_t)current->one;
		oh = (unsigned int *)current->two;
		ol = (int)current->three;
		ret = sys_llseek(fd, oh, ol, result, origin);
		current->one = current->two = current->three = 0;
	}
	else
	{
		grab_args(u, (long *)&fd, (long *)&oh, (long *)&ol);
		current->one = (long)fd;
		current->two = (long)oh;
		current->three = (long)ol;
		return;
	}
	put_ret(u, (long)ret);
}

void six_flock(struct pt_regs *u)
{
	unsigned int fd, cmd;
	int ret;
	grab_args(u, (long *)&fd, (long *)&cmd, 0);
	ret = sys_flock(fd, cmd);
	put_ret(u, (long)ret);
}

void six_msync(struct pt_regs *u)
{
	unsigned long start;
	size_t len;
	int flags, ret;
	grab_args(u, (long *)&start, (long *)&len, (long *)&flags);
	ret = sys_msync(start, len, flags);
	put_ret(u, (long)ret);
}

void six_readv(struct pt_regs *u)
{
	unsigned long fd;
	struct iovec *vec;
	long count;
	int ret;
	grab_args(u, (long *)&fd, (long *)&vec, (long *)&count);
	ret = sys_readv(fd, vec, count);
	put_ret(u, (long)ret);
}

void six_writev(struct pt_regs *u)
{
	unsigned long fd;
	struct iovec *vec;
	long count;
	int ret;
	grab_args(u, (long *)&fd, (long *)&vec, (long *)&count);
	ret = sys_writev(fd, vec, count);
	put_ret(u, (long)ret);
}

void six_getsid(struct pt_regs *u)
{
	pid_t p;
	int ret;
	grab_args(u, (long *)&p, 0, 0);
	ret = sys_getsid(p);
	put_ret(u, (long)ret);
}

void six_sysctl(struct pt_regs *u)
{
	struct __sysctl_args *args;
	int ret;
	grab_args(u, (long *)&args, 0, 0);
	ret = sys_sysctl(args);
	put_ret(u, (long)ret);
}

void six_fdatasync(struct pt_regs *u)
{
	unsigned int fd;
	int ret;
	grab_args(u, (long *)&fd, 0, 0);
	ret = sys_fdatasync(fd);
	put_ret(u, (long)ret);
}

void six_mlock(struct pt_regs *u)
{
	unsigned long start;
	size_t len;
	int ret;
	grab_args(u, (long *)&start, (long *)&len, 0);
	ret = sys_mlock(start, len);
	put_ret(u, (long)ret);
}

void six_munlock(struct pt_regs *u)
{
	unsigned long start;
	size_t len;
	int ret;
	grab_args(u, (long *)&start, (long *)&len, 0);
	ret = sys_munlock(start, len);
	put_ret(u, (long)ret);
}

void six_mlockall(struct pt_regs *u)
{
	int flags, ret;
	grab_args(u, (long *)&flags, 0, 0);
	ret = sys_mlockall(flags);
	put_ret(u, (long)ret);
}

void six_munlockall(struct pt_regs *u)
{
	int ret;
	ret = sys_munlockall();
	put_ret(u, (long)ret);
}

void six_sched_setparam(struct pt_regs *u)
{
	pid_t p;
	struct sched_param *param;
	int ret;
	grab_args(u, (long *)&p, (long *)&param, 0);
	ret = sys_sched_setparam(p, param);
	put_ret(u, (long)ret);
}

void six_sched_getparam(struct pt_regs *u)
{
	pid_t p;
	struct sched_param *param;
	int ret;
	grab_args(u, (long *)&p, (long *)&param, 0);
	ret = sys_sched_getparam(p, param);
	put_ret(u, (long)ret);
}

void six_sched_getscheduler(struct pt_regs *u)
{
	pid_t p;
	int ret;
	grab_args(u, (long *)&p, 0, 0);
	ret = sys_sched_setscheduler(p);
	put_ret(u, (long)ret);
}

void six_sched_setscheduler(struct pt_regs *u)
{
	pid_t p;
	int policy, ret;
	struct sched_param *param;
	grab_args(u, (long *)&p, (long *)&policy, (long *)&param);
	ret = sys_sched_setscheduler(p, policy, param);
	put_ret(u, (long)ret);
}

void six_sched_yield(struct pt_regs *u)
{
	int ret;
	ret = sys_sched_yield();
	put_ret(u, (long)ret);
}

void six_sched_get_priority_max(struct pt_regs *u)
{
	int policy, ret;
	grab_args(u, (long *)&policy, 0, 0);
	ret = sys_sched_get_priority_max(policy);
	put_ret(u, (long)ret);
}

void six_sched_get_priority_min(struct pt_regs *u)
{
	int policy, ret;
	grab_args(u, (long *)&policy, 0, 0);
	ret = sys_sched_get_priority_min(policy);
	put_ret(u, (long)ret);
}

void six_sched_rr_get_interval(struct pt_regs *u)
{
	pid_t p;
	struct timespec *interval;
	int ret;
	grab_args(u, (long *)&p, (long *)&interval, 0);
	ret = sys_sched_rr_get_interval(p, interval);
	put_ret(u, (long)ret);
}

void six_nanosleep(struct pt_regs *u)
{
	struct timespec *rqtp, *rmtp;
	int ret;
	grab_args(u, (long *)&rqtp, (long *)&rmtp, 0);
	ret = sys_nanosleep(rqtp, rmtp);
	put_ret(u, (long)ret);
}

void six_mremap(struct pt_regs *u)
{

	unsigned long addr, olen, nlen, flags;
	int ret;
	if (current->one)
	{
		grab_args(u, (long *)&flags, 0, 0);
		addr = current->one;
		olen = current->two;
		nlen = current->three;
		ret = sys_mremap(addr, olen, nlen, flags);
		current->one = current->two = current->three = 0;
	}
	else
	{
		grab_args(u, (long *)&addr, (long *)&olen, (long *)&nlen);
		current->one = (long)addr;
		current->two = (long)olen;
		current->three = (long)nlen;
		return;
	}
	put_ret(u, (long)ret);
}

void (* sys_call_table[])(struct pt_regs *) = {
		six_setup,
		six_exit,
		six_fork,
		six_read,
		six_write,
		six_open,
		six_close,
		six_waitpid, 
		six_creat,
		six_link,
		six_unlink,
		six_execve,
		six_chdir,
		six_time,
		six_mknod,
		six_chmod,
		six_chown,
		six_break,
		enosyscall,
		six_lseek,
		six_getpid,	// 20
		six_mount,
		six_umount,		
		six_setuid,
		six_getuid,
		six_stime,
		enosyscall,
		six_alarm,
		six_fstat,
		six_pause,	// 29
		six_utime,
		enosyscall,
		enosyscall,
		six_access,	// 33
		six_nice,
		enosyscall,
		six_sync,
		six_kill,
		six_rename,
		six_mkdir,
		six_rmdir,
		six_dup,
		six_pipe,
		six_times,
		enosyscall,
		six_brk,	// 45
		six_setgid,
		six_getgid,
		six_signal,
		six_geteuid,
		six_getegid,
		six_acct,
		enosyscall,
		enosyscall,
		six_ioctl,	// 54
		six_fcntl,
		enosyscall,
		six_setpgid,
		enosyscall,
		enosyscall,
		six_umask,
		six_chroot,
		six_ustat,
		six_dup2,
		six_getppid,	//64
		six_getpgrp,
		six_setsid,
		six_sigaction,
		six_sgetmask,
		six_ssetmask,
		six_setreuid,
		six_setregid,
		six_sigsuspend,
		six_sigpending,
		six_sethostname,
		six_setrlimit,
		six_getrlimit,
		six_getrusage,
		six_gettimeofday,
		six_settimeofday,
		six_getgroups,
		six_setgroups,
		six_select,
		six_symlink,
		enosyscall,
		six_readlink,
		six_uselib,
		six_swapon,
		six_reboot,
		six_readdir,
		six_mmap,
		six_munmap,	// 91
		six_truncate,
		six_ftruncate,
		six_fchmod,
		six_fchown,
		six_getpriority,
		six_setpriority,
		enosyscall,
		six_statfs,
		six_fstatfs,
		six_ioperm,
		six_socketcall,
		six_syslog,
		six_setitimer,
		six_getitimer,
		six_stat,
		six_lstat,
		six_fstat,
		six_olduname,
		enosyscall, 	// check out sys_iopl in asm/six/kernel/ioport.c!
		six_vhangup,
		six_idle,
		enosyscall,	// sys_vm86()!
		six_wait4,
		six_swapoff,
		six_sysinfo,
		six_ipc,
		six_fsync,
		six_sigreturn,		
		six_clone, 	//120
		six_setdomainname,
		six_uname,
		enosyscall,
		six_adjtimex,
		six_mprotect,
		six_sigprocmask,
		enosyscall,
		enosyscall,
		enosyscall,
		enosyscall,
		enosyscall,
		six_getpgid,
		six_fchdir,
		six_bdflush,
		enosyscall,		// Sysfs thingie. Will figure out later
		six_personality,
		enosyscall,		// some afs stuff, god knows what
		six_setfsuid,
		six_setfsgid,
		six_llseek,
		six_getdents,	//141
		six_newselect,	//142
		six_flock,
		six_msync,
		six_readv,
		six_writev,
		six_getsid,
		six_fdatasync,
		six_sysctl,
		six_mlock,
		six_munlock,
		six_mlockall,
		six_munlockall,
		six_sched_setparam,
		six_sched_getparam,
		six_sched_setscheduler,
		six_sched_getscheduler,
		six_sched_yield,
		six_sched_get_priority_max,
		six_sched_get_priority_min,
		six_sched_rr_get_interval,
		six_nanosleep,
		six_mremap,
		six_ps,		// 164 
		0		// End of shitlist
			       };

static unsigned char cache_21 = 0xff;
static unsigned char cache_A1 = 0xff;




extern so_sigset_t uni_lock;
extern so_sigset_t uni_unlock;

/*
 * This builds up the IRQ handler stubs using some ugly macros in irq.h
 *
 * These macros create the low-level assembly IRQ routines that do all
 * the operations that are needed to keep the AT interrupt-controller
 * happy. They are also written to be fast - and to disable interrupts
 * as little as humanly possible.
 *
 * NOTE! These macros expand to three different handlers for each line: one
 * complete handler that does all the fancy stuff (including signal handling),
 * and one fast handler that is meant for simple IRQ's that want to be
 * atomic. The specific handler is chosen depending on the SA_INTERRUPT
 * flag when installing a handler. Finally, one "bad interrupt" handler, that
 * is used when no handler is present.
 *
 * The timer interrupt is handled specially to insure that the jiffies
 * variable is updated at all times.  Specifically, the timer interrupt is
 * just like the complete handlers except that it is invoked with interrupts
 * disabled and should never re-enable them.  If other interrupts were
 * allowed to be processed while the timer interrupt is active, then the
 * other interrupts would have to avoid using the jiffies variable for delay
 * and interval timing operations to avoid hanging the system.
 */




static void (*interrupt[17])(void) = {
        IRQ0_interrupt, IRQ1_interrupt, IRQ2_interrupt, IRQ3_interrupt,
        IRQ4_interrupt, IRQ5_interrupt, IRQ6_interrupt, IRQ7_interrupt,
        IRQ8_interrupt, IRQ9_interrupt, IRQ10_interrupt, IRQ11_interrupt,
        IRQ12_interrupt, IRQ13_interrupt, IRQ14_interrupt, IRQ15_interrupt
#ifdef __SMP__
        ,IRQ16_interrupt
#endif
};

static void (*fast_interrupt[16])(void) = {
        fast_IRQ0_interrupt, fast_IRQ1_interrupt,
        fast_IRQ2_interrupt, fast_IRQ3_interrupt,
        fast_IRQ4_interrupt, fast_IRQ5_interrupt,
        fast_IRQ6_interrupt, fast_IRQ7_interrupt,
        fast_IRQ8_interrupt, fast_IRQ9_interrupt,
        fast_IRQ10_interrupt, fast_IRQ11_interrupt,
        fast_IRQ12_interrupt, fast_IRQ13_interrupt,
        fast_IRQ14_interrupt, fast_IRQ15_interrupt
};

static void (*bad_interrupt[16])(void) = {
        bad_IRQ0_interrupt, bad_IRQ1_interrupt,
        bad_IRQ2_interrupt, bad_IRQ3_interrupt,
        bad_IRQ4_interrupt, bad_IRQ5_interrupt,
        bad_IRQ6_interrupt, bad_IRQ7_interrupt,
        bad_IRQ8_interrupt, bad_IRQ9_interrupt,
        bad_IRQ10_interrupt, bad_IRQ11_interrupt,
        bad_IRQ12_interrupt, bad_IRQ13_interrupt,
        bad_IRQ14_interrupt, bad_IRQ15_interrupt
};


static inline void mask_irq(unsigned int irq_nr)
{               
        unsigned char mask;
	so_sigset_t temp;

	sigprocmask(SIG_SETMASK, &uni_lock, &temp);
	sosigaddset(&temp, irq_nr);
	sigprocmask(SIG_SETMASK, &temp, 0);
        mask = 1 << (irq_nr & 7);
        if (irq_nr < 8) {
                cache_21 |= mask;
                outb(cache_21,0x21);
        } else {        
                cache_A1 |= mask;
                outb(cache_A1,0xA1);
        }               
}                               
                        

static inline void unmask_irq(unsigned int irq_nr)
{
        unsigned char mask;
	so_sigset_t temp;

	sigprocmask(SIG_SETMASK, &uni_lock, &temp);
	sosigdelset(&temp, irq_nr);
	sigprocmask(SIG_SETMASK, &temp, 0);
	
        mask = ~(1 << (irq_nr & 7));
        if (irq_nr < 8) {
                cache_21 &= mask;
                outb(cache_21,0x21);
        } else {
                cache_A1 &= mask;
                outb(cache_A1,0xA1);
        }
}

void disable_irq(unsigned int irq_nr)
{
        unsigned long flags;

        save_flags(flags);
        cli();
        mask_irq(irq_nr);
        restore_flags(flags);
}

void enable_irq(unsigned int irq_nr)
{
        unsigned long flags;
        save_flags(flags);
        cli();
        unmask_irq(irq_nr);
        restore_flags(flags);
}




static void no_action(int cpl, void *dev_id, struct pt_regs *regs)
{
	printk("Caught Signal %d\n", cpl);
}

#ifdef __SMP__

/*
 * On SMP boards, irq13 is used for interprocessor interrupts (IPI's).
 */
static struct irqaction irq13 = { smp_message_irq, SA_INTERRUPT, 0, "IPI", NULL, NULL };

#else

/*
 * Note that on a 486, we don't want to do a SIGFPE on a irq13
 * as the irq is unreliable, and exception 16 works correctly
 * (ie as explained in the intel literature). On a 386, you
 * can't use exception 16 due to bad IBM design, so we have to
 * rely on the less exact irq13.
 *
 * Careful.. Not only is IRQ13 unreliable, but it is also
 * leads to races. IBM designers who came up with it should
 * be shot.
 */


static void math_error_irq(int cpl, void *dev_id, struct pt_regs *regs)
{
        outb(0,0xF0);
        if (ignore_irq13 || !hard_math)
                return;
        math_error();
}

static struct irqaction irq13 = { math_error_irq, 0, 0, "math error", NULL, NULL };

#endif


/*
 * IRQ2 is cascade interrupt to second interrupt controller
 */
static struct irqaction irq2  = { no_action, 0, 0, "cascade", NULL, NULL};

static struct irqaction irqWINCH = { winch_setsize, 0, 0, "SIGWINCH", NULL, NULL};

static void INT_action(void)
{
	printk("\nCaught SIGINT : Exiting\n");
	exit(SIGINT);
}
static struct irqaction irqINT  = { INT_action, 0, 0, "SIGINT", NULL, NULL};

static void KILL_action(void)
{
        printk("\nCaught SIGKILL : Exiting\n");
        exit(SIGKILL);
}
static struct irqaction irqKILL  = { KILL_action, 0, 0, "SIGKILL", NULL, NULL};


#if (__i386__)
#define IS_GUEST_USER_PC(p) \
	((unsigned long)(p) >= 0x03000000UL && (unsigned long)(p) < TASK_SIZE)
#endif

static void SEGV_action(int irq, void *dev_id, struct pt_regs *regs)
{
#if (__i386__)
	if (current && current->pid > 1 &&
	    current->user_mode && current->kernel_level == 1 &&
	    regs && IS_GUEST_USER_PC(regs->pc)) {
		printk("six: %s[%d]: segfault at %08x eip %08x esp %08x error %x\n",
		       current->comm, current->pid,
		       regs->cr2, regs->pc, regs->kesp, regs->uu2[5]);
		force_sig(SIGSEGV, current);
		return;
	}
#endif
	printk(KERN_EMERG "\nUnable to handle kernel paging request at virtual address %08x\n",
	       regs ? regs->cr2 : 0);
#if (__i386__)
	printk(KERN_EMERG "Oops: %04x\n", regs ? (regs->uu2[5] & 0xffff) : 0);
#endif
	show_regs(regs);
	panic("Fatal exception in kernel mode (SIGSEGV)");
}
static struct irqaction irqSEGV  = { SEGV_action, 0, 0, "SIGSEGV", NULL, NULL};

void system_call(int num, void *why, struct pt_regs *context);

static struct irqaction irqSYSCALL  = { system_call, 0, 0, "SYSCALL", NULL, NULL};

/*
 * Indexed by host signal number, not by PC IRQ line -- see NR_IRQS in
 * include/asm-six/irq.h.  Was a hardcoded [32], which the trap signal
 * (SIGRTMIN+4 == 38) indexed straight past.
 */
static struct irqaction *irq_action[NR_IRQS];

/*
 * do_IRQ handles IRQ's that have been installed without the
 * SA_INTERRUPT flag: it uses the full signal-handling return
 * and runs with other interrupts enabled. All relatively slow
 * IRQ's should use this format: notably the keyboard/timer
 * routines.
 */
asmlinkage void do_IRQ(int irq, struct pt_regs *context)
{
	struct irqaction * action;
	int do_random = 0;

	if (irq < 0 || irq >= NR_IRQS) {
		printk("six: do_IRQ: signal %d out of range\n", irq);
		return;
	}

	action = irq_action[irq];
#ifdef __SMP__
        /* IRQ 13 is allowed - that's a flush tlb */
        if(smp_threads_ready && active_kernel_processor!=smp_processor_id() && irq!=13)
                panic("fast_IRQ %d: active processor set wrongly(%d not %d).\n",
 			irq, active_kernel_processor, smp_processor_id());
#endif

        kstat.interrupts[irq]++;
#ifdef __SMP_PROF__
        int_count[smp_processor_id()][irq]++;
#endif
        while (action) {
                do_random |= action->flags;
                action->handler(irq, action->dev_id, context);
                action = action->next;
        }
        if (do_random & SA_SAMPLE_RANDOM)
                add_interrupt_randomness(irq);

}



void init_IRQ(void)
{
        int i;
        static unsigned char smptrap=0;
        struct itimerval tm;
        if(smptrap)
                return;
        smptrap=1;
#if (SIX)
	/*
	 * These two tables are vestigial -- SIX has no interrupt gates, and
	 * nothing ever calls through them -- but the loops used to write 32
	 * entries into interrupt[17] and, before that, ran off the end of
	 * fast_interrupt[16] as well.  Bound them by the actual array sizes.
	 */
	for(i = 0; i < (int)(sizeof(interrupt)/sizeof(interrupt[0])); i++)
		interrupt[i] = no_action;
	for(i = 0; i < (int)(sizeof(fast_interrupt)/sizeof(fast_interrupt[0])); i++)
		fast_interrupt[i] = no_action;
  	tm.it_value.tv_sec = 0;
	tm.it_interval.tv_sec = 0;
	tm.it_value.tv_usec = tm.it_interval.tv_usec = INTERVAL;
	setitimer(ITIMER_VIRTUAL, &tm, (char *)0);
#else /* in the real world... */
        /* set the clock to 100 Hz */
        outb_p(0x34,0x43);              /* binary, mode 2, LSB/MSB, ch 0 */
        outb_p(LATCH & 0xff , 0x40);    /* LSB */
        outb(LATCH >> 8 , 0x40);        /* MSB */
        for (i = 0; i < 16 ; i++)
                set_intr_gate(0x20+i,bad_interrupt[i]);
#endif
        /* This bit is a hack because we don't send timer messages to all processors yet */
        /* It has to be here .. it doesn't work if you put it down the bottom - assembler explodes 8) */
#ifdef __SMP__
        set_intr_gate(0x20+i, interrupt[i]);    /* IRQ '16' - IPI for rescheduling */
#endif
        request_region(0x20,0x20,"pic1");
        request_region(0xa0,0x20,"pic2");

        setup_x86_irq(31, &irq2);	// clock!
        setup_x86_irq(2, &irqINT);	// sigint
        setup_x86_irq(SIX_HOST_WINCHSIG, &irqWINCH);	// window resize
        setup_x86_irq(11, &irqSEGV);	// segv!
        setup_x86_irq(SIX_HOST_TRAPSIG, &irqSYSCALL);	// syscall
        setup_x86_irq(13, &irq13);	// math error
}

int setup_x86_irq(int irq, struct irqaction * new)
{
        int shared = 0;
        struct irqaction *old, **p;
        unsigned long flags;

        p = irq_action + irq;
        if ((old = *p) != NULL) {
                /* Can't share interrupts unless both agree to */
                if (!(old->flags & new->flags & SA_SHIRQ))
                        return -EBUSY;

                /* Can't share interrupts unless both are same type */
                if ((old->flags ^ new->flags) & SA_INTERRUPT)
                        return -EBUSY;

                /* add new interrupt at end of irq queue */
                do {
                        p = &old->next;
                        old = *p;
                } while (old);
                shared = 1;
        }
	/* This bit indicates that the generated interrupts can contribute
	 * to the entropy pool used by /dev/random and /dev/urandom. These
	 * devices return true random numbers which are in fact extracted
	 * from an entropy pool that is contributed from various random 
	 * events. If the device generates interrupts and truly random times,
	 * then this flag should be set. Devices prone to attacks should not
	 * set this flag - example is network drivers, which can be subjected 
	 * to predictable packet timing.
	 */
	
        if (new->flags & SA_SAMPLE_RANDOM)
                rand_initialize_irq(irq);

        save_flags(flags);
        cli();
        *p = new;

        if (!shared) {
	/*
	 * SA_INTERRUPT indicates a fast interrupt handler.
	 */
                if (new->flags & SA_INTERRUPT)
			set_intr_gate(0x20+irq,fast_interrupt[irq]);
                else
			set_intr_gate(0x20+irq,interrupt[irq]);
			
                unmask_irq(irq);
        }
        restore_flags(flags);
        return 0;
}

int request_irq(unsigned int irq,
                void (*handler)(int, void *, struct pt_regs *),
                unsigned long irqflags,
                const char * devname,
                void *dev_id)
{
        int retval;
        struct irqaction * action;

        if (!handler)
                return -EINVAL;

        action = (struct irqaction *)kmalloc(sizeof(struct irqaction), GFP_KERNEL);
        if (!action)
                return -ENOMEM;

        action->handler = handler;
        action->flags = irqflags;
        action->mask = 0;
        action->name = devname;
        action->next = NULL;
        action->dev_id = dev_id;

        retval = setup_x86_irq(irq, action);

        if (retval)
                kfree(action);
        return retval;
}

void ret_from_sys_call(struct pt_regs *context)
{
	if (bh_mask & bh_active)
		do_bottom_half();
	if (need_resched)
		schedule();
	if (current->signal)
	{
		if (current->signal & ~current->blocked)
		{
			/* work to do; there is an unblocked signal */
			unsigned long oldmask;
	
			if (current == task[0] || !user_mode(context)) /* no user signals to kernel threads */
				return;
	
			/* save original mask */
			oldmask = current->blocked;
	
			do_signal(oldmask, context);	
		}
	}
	/* else no unblocked signals - run */
}

/*
 * The interrupt entry point. 
 */
void sun_handler(int num, void *why, struct pt_regs *context)
{
	int ret;

	ENTER_KERNEL;
	current->signum = num;
	if (num != SIX_HOST_TRAPSIG && num != 14 && num != 26 && num != 29 && num != SIGSEGV)
		printk("six: HOST SIGNAL %d at pc=%08x (pid=%d)\n", num, context ? context->pc : 0, current ? current->pid : -1);

	if(current->kernel_level == 1)
	{
		/*
		 * We are coming from user land. So save our context
		 * in our process table entry.
		 *
		 * Subtle x86 Linux detail: RESTORE_USER_CONTEXT calls glibc's
		 * setcontext(&current->ucontext) after LEAVE_KERNEL has
		 * decremented kernel_level to 0.  Inside setcontext(), glibc
		 * first invokes rt_sigprocmask (int $0x80) to unmask signals
		 * BEFORE loading the guest %esp and %eip from ucontext.  If a
		 * pending SIGVTALRM or SIGIO is delivered at the instruction
		 * immediately following int $0x80 inside setcontext(),
		 * context->pc is still in host libc (< 0x03000000) and %esp is
		 * still on current->nsp, while current->ucontext ALREADY holds
		 * the true guest context (>= 0x03000000) that setcontext() was
		 * in the middle of restoring.  In that case, keep
		 * current->ucontext intact so RESTORE_USER_CONTEXT restarts
		 * setcontext(&current->ucontext) cleanly from the top.
		 */
#if (__i386__)
		if (!(!IS_GUEST_USER_PC(context->pc) &&
		      IS_GUEST_USER_PC(current->ucontext.pc)))
			SAVE_ALL;
#else
		SAVE_ALL;
#endif
		/*
		 * Save the old stack.
		 */
#if (!__i386__)
		__asm__("mov  %%sp, %0" : "=r" (current->osp));
#else
		/*
		 * x86 - backup ebp as well.
		 */
                __asm__("movl %%esp, (%1)" : "=r" (ret) : "r" (&current->osp));
                __asm__("movl %%ebp, (%1)" : "=r" (ret) : "r" (&current->obp));
#endif
		/*
		 * And load the new one.
		 */
#if (!__i386__)
		__asm__("mov %1, %%sp" : "=r" (ret) : "r" (current->nsp));
#else
                __asm__("movl (%1), %%esp" : "=r" (ret) : "r" (&current->nsp));
                __asm__("movl %esp, %ebp");
#endif
		do_IRQ(current->signum, &current->ucontext);
		ret_from_sys_call(&current->ucontext);
	} 
	else
	{
		/*
		 * We were already in the kernel (current->kernel_level > 1),
		 * on current->nsp, when an interrupt (e.g. SIGVTALRM after
		 * sti() in schedule()) arrived.  Handle the IRQ and return
		 * straight to the interrupted kernel frame.
		 */
		do_IRQ(current->signum, context);
	}


	/*
	 * Block host interrupt signals across LEAVE_KERNEL -> setcontext().
	 */
	cli();

	if(current->kernel_level == 1)
	{
#if (!__i386__)
		/*
		 * SPARC flushes register windows to %sp, so restore osp.
		 */
		__asm__("mov %1, %%sp" : "=r" (ret) : "r" (current->osp));
#endif
		LEAVE_KERNEL;
		/*
		 * Restore the context stored in current->ucontext. We are leaving
		 * for userland.
		 */
		RESTORE_USER_CONTEXT;
	}
	else
	{	
		LEAVE_KERNEL;
#if (__i386__)
		/*
		 * Return normally so the host kernel's rt_sigreturn restores
		 * context and uc_sigmask atomically without a user-space
		 * setcontext() window.
		 */
		return;
#else
		RESTORE_CONTEXT;
#endif
	}
}

#define SIX_FTRACE_MAX 16

struct six_ftrace_entry {
	unsigned long jiffies;
	int pid;
	char comm[16];
	int syscallnum;
	unsigned long pc;
	unsigned long arg1;
};

static struct six_ftrace_entry six_ftrace_ring[SIX_FTRACE_MAX];
static unsigned int six_ftrace_head = 0;
static unsigned int six_ftrace_total = 0;

static void six_ftrace_record(int syscallnum, struct pt_regs *context)
{
	struct six_ftrace_entry *e = &six_ftrace_ring[six_ftrace_head & (SIX_FTRACE_MAX - 1)];
	int i;

	e->jiffies = jiffies;
	e->pid = current ? current->pid : 0;
	if (current) {
		for (i = 0; i < 15 && current->comm[i]; i++)
			e->comm[i] = current->comm[i];
		e->comm[i] = '\0';
	} else {
		e->comm[0] = '?';
		e->comm[1] = '\0';
	}
	e->syscallnum = syscallnum;
	e->pc = context ? context->pc : 0;
	e->arg1 = context ? context->g3 : 0;
	six_ftrace_head++;
	six_ftrace_total++;
}

extern int six_host_sprint_symbol(unsigned long addr, char *buf, int buflen);

void show_ftrace(void)
{
	unsigned int count = (six_ftrace_total < SIX_FTRACE_MAX) ? six_ftrace_total : SIX_FTRACE_MAX;
	unsigned int start = six_ftrace_head - count;
	unsigned int i;
	char sym[64], *plus;

	printk("\nDumping ftrace buffer (%u recent syscall events):\n", count);
	for (i = 0; i < count; i++) {
		struct six_ftrace_entry *e = &six_ftrace_ring[(start + i) & (SIX_FTRACE_MAX - 1)];
		sym[0] = '\0';
		if (e->syscallnum >= 0 &&
		    e->syscallnum < (int)(sizeof(sys_call_table) / sizeof(sys_call_table[0])) &&
		    sys_call_table[e->syscallnum]) {
			six_host_sprint_symbol((unsigned long)sys_call_table[e->syscallnum], sym, sizeof(sym));
			for (plus = sym; *plus; plus++) {
				if (*plus == '+') {
					*plus = '\0';
					break;
				}
			}
		}
		printk("  [jiffies=%6lu] %-8s[%2d]: syscall=%3d (%-14s) eip=%08lx arg1=%08lx\n",
		       e->jiffies, e->comm, e->pid, e->syscallnum,
		       sym[0] ? sym : "?", e->pc, e->arg1);
	}
}

int ftrace_get_snapshot(char *dst, int maxlen)
{
	unsigned int count = (six_ftrace_total < SIX_FTRACE_MAX) ? six_ftrace_total : SIX_FTRACE_MAX;
	unsigned int start = six_ftrace_head - count;
	unsigned int i;
	int pos = 0;
	char sym[64], *plus;

	if (!dst || maxlen <= 64)
		return 0;
	pos += sprintf(dst + pos, "# tracer: syscall\n# entries-in-buffer: %u\n", count);
	for (i = 0; i < count && pos + 96 < maxlen; i++) {
		struct six_ftrace_entry *e = &six_ftrace_ring[(start + i) & (SIX_FTRACE_MAX - 1)];
		sym[0] = '\0';
		if (e->syscallnum >= 0 &&
		    e->syscallnum < (int)(sizeof(sys_call_table) / sizeof(sys_call_table[0])) &&
		    sys_call_table[e->syscallnum]) {
			six_host_sprint_symbol((unsigned long)sys_call_table[e->syscallnum], sym, sizeof(sym));
			for (plus = sym; *plus; plus++) {
				if (*plus == '+') {
					*plus = '\0';
					break;
				}
			}
		}
		pos += sprintf(dst + pos,
			       "  [jiffies=%6lu] %-8s[%2d]: syscall=%3d (%-14s) eip=%08lx arg1=%08lx\n",
			       e->jiffies, e->comm, e->pid, e->syscallnum,
			       sym[0] ? sym : "?", e->pc, e->arg1);
	}
	dst[pos] = '\0';
	return pos;
}

void system_call(int num, void *why, struct pt_regs *context)
{
	int syscallnum;
#if (__i386__)
	struct six_guest_call *gc = NULL;
#endif

	/*
	 * Load the syscall "registers" into the context.
	 *
	 * On SPARC these arrive for free: %g2..%g5 are real registers and
	 * the host saves them into the ucontext when it delivers the
	 * signal.  On x86 they used to arrive for free as well, by way of
	 * the MMX-registers-alias-the-FPU-save-area trick described in
	 * include/asm-six/sixcall.h -- but Linux zeroes the FPU state
	 * before entering a signal handler, so nothing survives the trap.
	 *
	 * On x86 there are now two sources, and which one applies depends
	 * on who trapped:
	 *
	 *   Kernel  kernel_thread() and the exit path in
	 *           kernel_thread_start() reach us through raise(), which
	 *           is a libc call.  %esi is callee-saved, so its value at
	 *           the inner "int $0x80" is not ours to choose, and the
	 *           arguments come from the six_call global instead.
	 *
	 *   Guest   a separately linked ELF executable running in the
	 *           emulated RAM.  It cannot see the six_call symbol, so it
	 *           leaves the address of its own argument block in %esi.
	 *           Guest virtual addresses are host virtual addresses in
	 *           SIX -- set_proc_mappings() maps every page of a task at
	 *           its own guest address -- so the block is directly
	 *           readable here.
	 *
	 * Either way the values end up in context->g2..g7, so grab_args(),
	 * put_ret() and all 165 entries of sys_call_table continue to read
	 * what they have always read and neither know nor care where it
	 * came from.
	 */
#if (__i386__)
	if (user_mode(context)) {
		unsigned long p = context->esi;

		/*
		 * Anything a guest hands us is untrusted.  The block has to
		 * lie wholly inside the guest address space; TASK_SIZE is
		 * where the guest's world ends (include/asm-six/processor.h,
		 * = STACK_BASE).  A guest that gets this wrong gets EFAULT,
		 * not a kernel fault on a wild pointer.
		 */
		if (p == 0 || p >= TASK_SIZE ||
		    p + sizeof(struct six_guest_call) > TASK_SIZE) {
			printk("six: guest %d passed a bad syscall block %08lx\n",
			       current->pid, p);
			context->g2 = -EFAULT;
			return;
		}

		gc = (struct six_guest_call *)p;

		/*
		 * Note the reordering.  The guest block is in natural
		 * argument order; pt_regs inherited an inversion from the
		 * way the 2005 code packed the mm2 register, where g4 held
		 * argument 3 and g5 argument 2.  grab_args() compensates for
		 * it, so it has to be reproduced here rather than fixed.
		 */
		context->g2 = gc->nr;
		context->g3 = gc->a1;
		context->g4 = gc->a3;
		context->g5 = gc->a2;
		context->g6 = 0;

		/*
		 * Arguments 4 to 6 have nowhere to go in the old three
		 * argument channel.  They are carried through anyway so that
		 * the six-argument calls can eventually be done in one trap.
		 * At the moment nothing reads them: six_mmap() still uses the
		 * 2005 two-call protocol, stashing the first three arguments
		 * in current->one/two/three and expecting the guest to trap a
		 * second time with the rest.
		 */
		context->g7 = gc->a4;
		context->g8 = gc->a5;
		context->g9 = gc->a6;

		/*
		 * Build with -DSIX_TRACE_GUEST_SYSCALLS=1 to see every call a
		 * guest makes.  There is no other way to watch a guest from
		 * outside: host strace only ever sees the getpid/kill pair
		 * that raises the trap, never what the trap was for.
		 */
#if SIX_TRACE_GUEST_SYSCALLS
		printk("guest[%d] syscall %d (%08lx %08lx %08lx)\n",
		       current->pid, (int)gc->nr, gc->a1, gc->a2, gc->a3);
#endif
	} else {
		context->g2 = six_call.g2;
		context->g3 = six_call.g3;
		context->g4 = six_call.g4;
		context->g5 = six_call.g5;
		context->g6 = six_call.g6;
		context->g7 = six_call.g7;
	}
#endif

	syscallnum = context->g2;

	/*
	 * SIX has no NR_syscalls of its own -- the table is just however
	 * many entries the initialiser at line ~1558 happens to have.
	 */
	if (syscallnum < 0 ||
	    syscallnum >= (int)(sizeof(sys_call_table) / sizeof(sys_call_table[0])) ||
	    sys_call_table[syscallnum] == 0) {
		printk("six: bad syscall %d\n", syscallnum);
		context->g2 = -ENOSYS;
#if (__i386__)
		if (gc)
			gc->ret = context->g2;
		else
			six_call.g2 = context->g2;
#endif
		return;
	}

	/*
	 * Call the system call worker
	 */
	six_ftrace_record(syscallnum, context);
	cli();
	sys_call_table[syscallnum](context);
	sti();

	/*
	 * put_ret() left the return value in context->g2; hand it back to
	 * the caller through whichever channel it arrived on.
	 */
	if (context->g2 == -ERESTARTSYS ||
	    context->g2 == -ERESTARTNOINTR ||
	    context->g2 == -ERESTARTNOHAND) {
		context->g2 = -EINTR;
	}

#if (__i386__)
	if (gc)
		gc->ret = context->g2;
	else
		six_call.g2 = context->g2;
#endif
}
