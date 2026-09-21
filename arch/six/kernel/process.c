
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

#include <asm/sixcall.h>
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
#ifdef CONFIG_NET
                {
                        extern void six_eth_poll(void);
                        six_eth_poll();
                }
#endif
                six_host_sti();
                six_host_idle_sleep();
                schedule();
        }
}

void hard_reset_now(void) 
{  
	reset_sun_tty();
	exit(1);
}

#include "host.h"

extern char _start[], _etext[];
int oops_in_progress = 0;

static int is_kernel_text(unsigned long addr)
{
	return (addr >= (unsigned long)_start && addr < (unsigned long)_etext);
}

void print_kaddr(unsigned long addr)
{
	char sym[96];
	if (six_host_sprint_symbol(addr, sym, sizeof(sym)))
		printk("[<%08lx>] <%s>", addr, sym);
	else
		printk("[<%08lx>]", addr);
}

void show_trace_ebp(unsigned long ebp, unsigned long stack_low, unsigned long stack_high)
{
	int depth = 0;
	while (stack_low && ebp >= stack_low && (ebp + 8) <= stack_high &&
	       (ebp & 3) == 0 && depth < 16) {
		unsigned long *frame = (unsigned long *)ebp;
		unsigned long next_ebp = frame[0];
		unsigned long ret_pc = frame[1];
		if (!ret_pc)
			break;
		printk(" ");
		print_kaddr(ret_pc);
		printk("\n");
		depth++;
		if (next_ebp <= ebp)
			break;
		ebp = next_ebp;
	}
	if (depth == 0)
		printk("  <none>\n");
}

static void get_task_stack_bounds(struct task_struct *p, unsigned long ebp,
				  unsigned long *low_out, unsigned long *high_out)
{
	unsigned long low = p->kernel_stack_page;
	unsigned long high = low ? (low + DEFAULT_STACK_SIZE) : 0;

	if (low && ebp >= low && (ebp + 8) <= high) {
		*low_out = low;
		*high_out = high;
		return;
	}
	if (p->mm && p->mm->start_stack) {
		low = p->mm->start_stack;
		high = low + DEFAULT_STACK_SIZE;
		if (ebp >= low && (ebp + 8) <= high) {
			*low_out = low;
			*high_out = high;
			return;
		}
	}
	if (p->kcontext.kesp && ebp >= p->kcontext.kesp &&
	    (ebp - p->kcontext.kesp) < 0x4000UL) {
		*low_out = p->kcontext.kesp;
		*high_out = (p->kcontext.kesp & ~0x1fffUL) + 0x4000UL;
		return;
	}
	*low_out = low;
	*high_out = high;
}

unsigned long six_task_saved_pc(struct task_struct *p)
{
	unsigned long stack_low, stack_high, ebp;
	if (!p)
		return 0;
	ebp = p->kcontext.ebp;
	get_task_stack_bounds(p, ebp, &stack_low, &stack_high);
	if (stack_low && ebp >= stack_low && (ebp + 8) <= stack_high && (ebp & 3) == 0) {
		unsigned long ret_pc = ((unsigned long *)ebp)[1];
		if (ret_pc)
			return ret_pc;
	}
	if (p->kcontext.pc)
		return p->kcontext.pc;
	return p->ucontext.pc;
}

void show_task_trace(struct task_struct *p)
{
	unsigned long stack_low = 0, stack_high = 0, ebp;
	int depth = 0;

	if (!p)
		return;

	printk("   Call Trace:");
	if (p == current) {
#if (__i386__)
		__asm__ __volatile__("movl %%ebp, %0" : "=r"(ebp));
		get_task_stack_bounds(p, ebp, &stack_low, &stack_high);
		if (!stack_low || ebp < stack_low || ebp >= stack_high) {
			unsigned long esp;
			__asm__ __volatile__("movl %%esp, %0" : "=r"(esp));
			stack_low = esp & ~0x1fffUL;
			stack_high = stack_low + 0x4000UL;
		}
#else
		ebp = 0;
#endif
	} else {
		ebp = p->kcontext.ebp;
		get_task_stack_bounds(p, ebp, &stack_low, &stack_high);
		if (p->kcontext.pc && is_kernel_text(p->kcontext.pc)) {
			printk(" ");
			print_kaddr(p->kcontext.pc);
			depth++;
		}
	}

	while (stack_low && ebp >= stack_low && (ebp + 8) <= stack_high &&
	       (ebp & 3) == 0 && depth < 12) {
		unsigned long *frame = (unsigned long *)ebp;
		unsigned long next_ebp = frame[0];
		unsigned long ret_pc = frame[1];
		if (!ret_pc)
			break;
		printk(" ");
		print_kaddr(ret_pc);
		depth++;
		if (next_ebp <= ebp)
			break;
		ebp = next_ebp;
	}
	if (p->user_mode && p->ucontext.pc) {
		printk(" [<%08x>] (user esp=%08x)", p->ucontext.pc, p->ucontext.kesp);
		depth++;
	}
	if (depth == 0)
		printk(" <none>");
	printk("\n");
}

void dump_stack(void)
{
	unsigned long ebp = 0, esp = 0;
	unsigned long stack_low, stack_high;
	unsigned long *sp;
	int i;

#if (__i386__)
	__asm__ __volatile__("movl %%ebp, %0\n\tmovl %%esp, %1"
			     : "=r"(ebp), "=r"(esp));
#endif
	if (current) {
		printk("Pid: %d, comm: %s\n", current->pid, current->comm);
		stack_low = current->kernel_stack_page;
		stack_high = stack_low + DEFAULT_STACK_SIZE;
		if (esp < stack_low || esp >= stack_high) {
			stack_low = esp;
			stack_high = (esp & ~0x1fffUL) + 0x4000UL;
		}
	} else {
		stack_low = esp;
		stack_high = (esp & ~0x1fffUL) + 0x4000UL;
	}

	if (esp) {
		sp = (unsigned long *)esp;
		printk("Stack:");
		for (i = 0; i < 24; i++) {
			if ((unsigned long)(sp + i + 1) > stack_high)
				break;
			if ((i % 8) == 0)
				printk("\n       ");
			printk("%08lx ", sp[i]);
		}
		printk("\n");
	}

	printk("Call Trace:\n");
	show_trace_ebp(ebp, stack_low, stack_high);
}

extern unsigned long intr_count;
extern unsigned long kernel_counter;

void show_all_cpu_bt(void)
{
	unsigned long ebp = 0, esp = 0;
	unsigned long stack_low, stack_high;

#if (__i386__)
	__asm__ __volatile__("movl %%ebp, %0\n\tmovl %%esp, %1"
			     : "=r"(ebp), "=r"(esp));
#endif
	printk("\nSending NMI / dumping backtrace for all CPUs:\n");
	printk("CPU#0 [online, kernel_counter=%lu, intr_count=%lu, need_resched=%d, esp=%08lx, ebp=%08lx]:\n",
	       kernel_counter, intr_count, need_resched, esp, ebp);
	if (current) {
		printk("  current: %s (pid=%d, state=%ld, kernel_level=%d, user_mode=%d)\n",
		       current->comm, current->pid, current->state,
		       current->kernel_level, current->user_mode);
		stack_low = current->kernel_stack_page;
		stack_high = stack_low + DEFAULT_STACK_SIZE;
		if (esp < stack_low || esp >= stack_high) {
			stack_low = esp;
			stack_high = (esp & ~0x1fffUL) + 0x4000UL;
		}
	} else {
		stack_low = esp;
		stack_high = (esp & ~0x1fffUL) + 0x4000UL;
	}
	show_trace_ebp(ebp, stack_low, stack_high);
}

void show_regs(struct pt_regs * regs)
{
	unsigned long stack_low = 0, stack_high = 0;
	int i;

	if (!regs)
		return;
	oops_in_progress = 1;
#if (__i386__)
	printk("\nEIP: %04x:", regs->cs & 0xffff);
	print_kaddr(regs->pc);
	printk(" EFLAGS: %08x CR2: %08x\n", regs->psw, regs->cr2);
	printk("EAX: %08x EBX: %08x ECX: %08x EDX: %08x\n",
	       regs->uu2[3], regs->uu2[0], regs->uu2[2], regs->uu2[1]);
	printk("ESI: %08x EDI: %08x EBP: %08x ESP: %08x\n",
	       regs->esi, regs->edi, regs->ebp, regs->kesp);
	printk(" DS: %04x  ES: %04x  FS: %04x  GS: %04x  SS: %04x\n",
	       regs->ds & 0xffff, regs->es & 0xffff,
	       regs->fs & 0xffff, regs->gs & 0xffff, regs->ss & 0xffff);
#else
	printk("\nPC: [<%08x>] NPC: [<%08x>] PSR: %08x SP: %08x\n",
	       regs->pc, regs->npc, regs->psw, regs->esp);
#endif
	if (current) {
		printk("Process %s (pid: %d, stackpage=%08lx, kernel_level: %d, user_mode: %d)\n",
		       current->comm, current->pid, current->kernel_stack_page,
		       current->kernel_level, current->user_mode);
		stack_low = current->kernel_stack_page;
		stack_high = stack_low + DEFAULT_STACK_SIZE;
	}
#if (__i386__)
	if (regs->kesp) {
		unsigned long *sp = (unsigned long *)regs->kesp;
		if (regs->kesp < stack_low || regs->kesp >= stack_high) {
			stack_low = regs->kesp;
			stack_high = (regs->kesp & ~0x1fffUL) + 0x4000UL;
		}
		printk("Stack:");
		for (i = 0; i < 24; i++) {
			if ((unsigned long)(sp + i + 1) > stack_high)
				break;
			if ((i % 8) == 0)
				printk("\n       ");
			printk("%08lx ", sp[i]);
		}
		printk("\n");
	}
	printk("Call Trace:\n");
	if (regs->pc) {
		printk(" ");
		print_kaddr(regs->pc);
		printk("\n");
	}
	show_trace_ebp(regs->ebp, stack_low, stack_high);
	if (is_kernel_text(regs->pc) ||
	    (regs->pc >= 0x03000000UL && regs->pc + 16 <= TASK_SIZE)) {
		unsigned char *code = (unsigned char *)regs->pc;
		printk("Code: ");
		for (i = 0; i < 16; i++)
			printk("%02x ", code[i]);
		printk("\n");
	}
#endif
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


/*
 * Repair a relocated host context.
 *
 * glibc's ucontext_t is self-referential: getcontext() (and the kernel,
 * when it builds a signal frame) sets uc_mcontext.fpregs to point at the
 * FPU save area *embedded in that same structure*.  The moment a context
 * is memcpy'd somewhere else -- which is exactly what copy_thread() does,
 * and what SIX does all over the place -- that pointer still refers to the
 * original, which for a signal frame is a stack address that is about to
 * be reused.
 *
 * setcontext()/swapcontext() follow the pointer to restore the FPU state,
 * so leaving it dangling means loading floating-point state out of
 * whatever happens to be on the old stack. Call this after every copy.
 *
 * (This is not a bug the 2005 code could have had: Solaris embeds the FPU
 * state in the ucontext by value.)
 */
void six_fix_context(struct pt_regs *p)
{
#if (__i386__)
	p->fpregs = (unsigned int) &p->fpregs_mem[0];
	memset(&p->fpregs_mem[0], 0, sizeof(p->fpregs_mem));
	p->fpregs_mem[0] = 0x037f; /* cw: all exceptions masked */
	p->fpregs_mem[1] = 0x0000; /* sw: no exceptions pending */
	p->fpregs_mem[2] = 0xffff; /* tw: all tags empty */
#endif
}

void copy_thread(int nr, unsigned long clone_flags, unsigned long esp, struct task_struct * p, struct pt_regs * regs)
{
	/*
	 * Why this?
	 * Because, when this new child gets scheduled onto the cpu
	 * for the first time, it lands up straight into userland.
	 * If you look into copy_thread(), you will find that the
	 * user context of the parent process is copied to the
	 * kernel context of the child; now the scheduler always
	 * restores the kernel context, and hence a child shoots
	 * straight to userland when it's scheduled for the first
	 * time. For more info on what ucontext and kcontext are,
	 * check comments in linux/sched.h.
	 */
	p->kernel_level = 0;
	/*
	 * This is basically to ensure that makecontext() does a neat job.
	 * We need to handover a "legal" initial context to makecontext() so that
	 * it can convert it to what we want. Now imagine that this is the first
	 * fork() in the system; do_fork() has already copied the whole parent
	 * task_struct to the child task_struct; I.e., the child kcontext is now
	 * a copy of the parents kcontext; but who is the parent? If this is the
	 * first fork, then the parent is the init process. But the init process
	 * has never been scheduled before - remember, this is the first fork,
	 * so there were no other processes - and hence its kcontext would only
	 * contain garbage. (because schedule() is the guy who fills up kcontext
	 * meaningfully. So it is not safe to rely on the parents kcontext; so
	 * rely on the parents ucontext instead; after all, the child lands
	 * up straight in userland when scheduled in for the first time.
	 */
	memcpy(&p->kcontext, regs, sizeof(struct pt_regs));
	six_fix_context(&p->kcontext);
	if(!current->is_mapped)
	{

	/*
	 * This is a request for a new kernel_thread.
	 * Now when the turn comes for this task table entry to be
	 * scheduled and run, this context will be put into the cpu,
	 * and the control will go to kernel_thread_start.
	 */
		p->kcontext.pc = (unsigned int) kernel_thread_start;
		p->mm->start_stack = alloc_stack();
#if (__i386__)
		/*
		 * Set BOTH stack pointer slots in ucontext_t.
		 *
		 * glibc's setcontext()/swapcontext() on i386 reloads %esp
		 * from gregs[REG_ESP] (our "kesp", word 12) and ignores
		 * gregs[REG_UESP] (our "esp", word 22) completely.  Setting
		 * only "esp" left "kesp" pointing at task[0]'s host stack,
		 * so init, bdflush and kswapd all ran on the same stack and
		 * trampled each other's call frames the moment bdflush or
		 * kswapd woke up (for example, when halt's sys_kill(-1,
		 * SIGKILL) woke bdflush from interruptible_sleep_on).
		 */
		p->kcontext.esp  = (p->mm->start_stack + DEFAULT_STACK_SIZE - 16) & ~15UL;
		p->kcontext.kesp = p->kcontext.esp;
		p->kcontext.ebp  = p->kcontext.esp;
#else
		p->kcontext.esp = p->mm->start_stack + DEFAULT_STACK_SIZE - SPARC_FRAME;
		p->kcontext.npc = p->kcontext.pc + 4;
#endif
	}
	else
	{
	/*
	 * Just a normal fork. So modify the childs context such that the return
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
 
#if (!__i386__)
	int ret;
#endif
/*
 * Ok, what we need here is to first put 0 into g7. Put clone system call
 * number into g2. Now we have g3, g4, g5 and g6 for other stuff like 
 * storing the arguments, etc. Now send the syscall signal using raise.
 * The signal gets sent, caught by system_call() which checks the number
 * in g2 and calls sys_clone(). Inside over there, it creates a new task
 * struct entry and so on. Before putting the context into the task entry,
 * g7 is made 1. Indicates to the child what it is.
 * Anyway the stuff that follows is a lil messy..
 */




#if (__i386__)
	/*
	 * This used to be three movq's into mm0/mm2/mm4:
	 *
	 *      mm0 = (KERNEL_THREAD_REQUEST << 32) | 120   (120 = clone)
	 *      mm2 = (arg << 32) | fn
	 *      mm4 = flags
	 *
	 * which the trap handler then read back out of the FPU image in
	 * the saved context.  Linux wipes the FPU on the way into a signal
	 * handler, so the same values now go through the memory channel.
	 * Field-for-field identical, just somewhere the host cannot erase
	 * them -- see include/asm-six/sixcall.h.
	 */
	six_call.g2 = 120;                      /* __NR_clone            */
	six_call.g3 = KERNEL_THREAD_REQUEST;    /* was mm0's high half   */
	six_call.g4 = (unsigned long) fn;       /* was mm2's low half    */
	six_call.g5 = (unsigned long) arg;      /* was mm2's high half   */
	six_call.g6 = flags;                    /* was mm4               */
	six_call.g7 = 0;

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
 * If we return, we are the parent. The child gets redirected with a new
 * stack to kernel_thread_start. And how that happens is, the copy_thread
 * called inside do_fork() does a makecontext() on the context structure
 * before putting the context inside the newly created task table entry.
 * And the function pointer passed to makecontext is kernel_thread_start.
 * So when the times comes for the newly created task table entry to be
 * brought into action, the setcontext on that context jumps control to
 * to kernel_thread_start.
 */
#if (__i386__)
	/* sys_clone()'s return value comes back through the channel. */
	return (pid_t) six_call.g2;
#else
	return 0;
#endif
}

/*
 * Ok the child starts here.
 */
void kernel_thread_start()
{

	int (*fun)(void *);
	int *args;

#if (__i386__)
	/*
	 * Recover the function and argument that kernel_thread() passed.
	 *
	 * The old code read them back out of mm2, which worked because
	 * setcontext() restored the child's saved FPU state -- and hence
	 * its MMX registers -- as it scheduled the child in for the first
	 * time.  That gave each child its own private copy.
	 *
	 * The memory channel cannot be read directly here: by the time the
	 * child actually runs, any number of other syscalls will have
	 * overwritten six_call.  But copy_thread() memcpy'd the whole trap
	 * context (channel values included) into p->kcontext, and g4/g5
	 * sit *past the end of the host ucontext_t*, so swapcontext()
	 * leaves them alone.  The child's private copy is therefore right
	 * here, in its own task struct -- the same per-task delivery, by
	 * other means.
	 */
	fun  = (int (*)(void *)) current->kcontext.g4;
	args = (int *)           current->kcontext.g5;
#else
	__asm__("mov %%g4, %0" : "=r" (args)); 

	__asm__("mov %%g5, %0" : "=r" (fun)); 
#endif
	/* I'm the child, do the work - call the function */
		
	(*fun)(args);

	/* work done - now prepare to call exit() */

#if (__i386__)
	six_call.g2 = 1;        /* 1 is the exit system call number */
	six_call.g3 = 0;        /* exit status                      */
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

