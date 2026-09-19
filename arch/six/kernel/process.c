
/*
 *  linux/arch/i386/kernel/process.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#define __KERNEL_SYSCALLS__

#include <solaris.h>

#include <stdarg.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/ldt.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/interrupt.h>
#include <linux/config.h>
#include <linux/unistd.h>

#include <asm/segment.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/io.h>
#include <linux/smp.h>


asmlinkage void ret_from_sys_call(struct pt_regs *);

asmlinkage int sys_idle(void)
{
        if (current->pid != 0)
                return -EPERM;

        /* endless idle loop with no priority at all */
        current->counter = -100;
        for (;;) {
                schedule();
        }
}

void hard_reset_now(void) 
{  
	reset_sun_tty();
	exit(1);
}

void show_regs(struct pt_regs * regs)
{
	/*
	 * TODO. later.
	 */
}

/*
 * Free current thread data structures etc..
 */

void exit_thread(void)
{
	/* coming soon :) */
}

void flush_thread()
{
	/* coming soon :) */
}

void kernel_thread_start();


void copy_thread(int nr, unsigned long clone_flags, unsigned long esp, struct task_struct * p, struct pt_regs * regs)
{
	/*
	 * why this?
	 * because, when this new child gets scheduled onto the cpu
	 * for the first time, it lands up straight into userland.
	 * if you look into copy_thread(), you will find that the
	 * user context of the parent process is copied to the
	 * kernel context of the child; now the scheduler always
	 * restores the kernel context, and hence a child shoots
	 * straight to userland when its scheduled for the first
	 * time. for more info on what ucontext and kcontext are,
	 * check comments in linux/sched.h.
	 */
	p->kernel_level = 0;
	/*
	 * this is basically to ensure that makecontext() does a neat job.
	 * we need to handover a "legal" initial context to makecontext() so that
	 * it can convert it to what we want. now imagine that this is the first
	 * fork() in the system; do_fork() has already copied the whole parent
	 * task_struct to the child task_struct; ie, the child kcontext is now
	 * a copy of the parents kcontext; but who is the parent? if this is the
	 * first fork, then the parent is the init process. but the init process
	 * has never been scheduled before - remember, this is the first fork,
	 * so there were no other processes - and hence its kcontext would only
	 * contain garbage. (because schedule() is the guy who fills up kcontext
	 * meaningfully. so it is not safe to rely on the parents kcontext; so
	 * rely on the parents ucontext instead; after all, the child lands
	 * up straight in userland when scheduled in for the first time.
	 */
	memcpy(&p->kcontext, regs, sizeof(struct pt_regs));
	if(!current->is_mapped)
	{

	/*
	 * this is a request for a new kernel_thread.
	 * now when the turn comes for this task table entry to be
	 * scheduled and run, this context will be put into the cpu,
	 * and the control will go to kernel_thread_start.
	 */
		p->kcontext.pc = kernel_thread_start;
		p->mm->start_stack = alloc_stack();
#if (__i386__)
		p->kcontext.esp = p->mm->start_stack + DEFAULT_STACK_SIZE - 4;
#else
		p->kcontext.esp = p->mm->start_stack + DEFAULT_STACK_SIZE - SPARC_FRAME;
		p->kcontext.npc = p->kcontext.pc + 4;
#endif
	}
	else
	{
	/*
	 * just a normal fork. so modify the childs context such that the return
	 * register contains a zero.
	 */
		p->kcontext.g2 = 0;
	}
}       

/*
 * This is the mechanism for creating a new kernel thread.
 *
 * NOTE! Only a kernel-only process(ie the swapper or direct descendants
 * who haven't done an "execve()") should use this: it will work within
 * a system call from a "real" process, but the process memory space will
 * not be free'd until both the parent and the child have exited.
 */
pid_t kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{
 
	int ret;
#if (__i386__)
	long long val;
	long *valp;
	valp = &val;
#endif
/*
 * ok,what we need here is to first put 0 into g7.put clone systemcall
 * number into g2.now we have g3,g4,g5 and g6 for other stuff like 
 * storing the arguments,etc.now send the syscall signal using raise.
 * the signal gets sent,caught by system_call() which checks the number
 * in g2 and calls sys_clone().inside over there,it creates a new task
 * struct entry and so on.before putting the context into the task entry,
 * g7 is made 1.indicates to the child what it is.
 * anyway the stuff that follows is a lil messy..
 */




#if (__i386__)
	/* 
	 * this takes care of the system call number and the flag 
	 * that this is a request from kernel_thread.both are 
	 * combined into a single 8 byte strip.
	 */

	val = KERNEL_THREAD_REQUEST;
	val <<= 32;
	val += 120;
	__asm__("movq (%1), %%mm0" : "=r" (ret) : "r" (valp));

	/*
	 * here we combine the function pointer and arguments into
	 * one.
	 */

	val = arg;
	val <<= 32;
	val += fn;
	__asm__("movq (%1), %%mm2" : "=r" (ret) : "r" (valp));

	/*
	 * and here goes the flags.
	 */

	val = flags;
	__asm__("movq (%1), %%mm4" : "=r" (ret) : "r" (valp));

#else
	/*
	 * sparc stuff :
	 */
 	__asm__("mov 120, %g2\n");
	__asm__("mov %1, %%g3"   :  "=r" (ret)  : "r" (flags) );
	__asm__("mov %1, %%g4"   :  "=r" (ret)  : "r" (arg) );
	__asm__("mov %1, %%g5"   :  "=r" (ret)  : "r" (fn) );
 	__asm__("mov %1, %%g6"	 :  "=r" (ret)  : "r" (KERNEL_THREAD_REQUEST) );

#endif


/* now that the registers are all set up, trigger the system call */
	
	INT_SYSCALL;
/*
 * if we return, we are the parent.the child gets redirected with a new
 * stack to kernel_thread_start.and how that happens is, the copy_thread
 * called inside do_fork() does a makecontext() on the context structure
 * before putting the context inside the newly created task table entry.
 * and the function pointer passed to makecontext is kernel_thread_start.
 * so when the times comes for the newly created task table entry to be
 * brought into action,the setcontext on that context jumps control to
 * to kernel_thread_start.
 */
	return;
}

/*
 * Ok the child starts here.
 */
void kernel_thread_start()
{

	int (*fun)(void *);
	int *args;

#if (__i386__)
	long long get;
	long *getp;
	int ret;
	getp = &get;

	__asm__("movq %%mm2, (%1)" : "=r" (ret) : "r" (getp));
	fun = get;
	get >>= 32;
	args = get;
#else
	__asm__("mov %%g4, %0" : "=r" (args)); 

	__asm__("mov %%g5, %0" : "=r" (fun)); 
#endif
	/* im the child,do the work - call the function */
		
	(*fun)(args);

	/* work done - now prepare to call exit() */

#if (__i386__)
	get = 1;
	__asm__("movq (%1), %%mm0" : "=r" (ret) : "r" (getp));
#else
 	__asm__("mov 1, %g2\n"); /* 1 is the exit system call number */
#endif

	INT_SYSCALL; 
	
	/* should not return here - the thread dies */
}

void release_thread(struct task_struct *dead_task)
{
	/* coming soon! */
}

