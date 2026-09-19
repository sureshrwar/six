
#ifndef _LINUX_SCHED_H
#define _LINUX_SCHED_H

#include <asm/param.h>	/* for HZ */

extern unsigned long event;

#include <linux/binfmts.h>
#include <linux/personality.h>
#include <linux/tasks.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/page.h>

#include <linux/smp.h>
#include <linux/tty.h>

/*
 * cloning flags:
 */
#define CSIGNAL         0x000000ff      /* signal mask to be sent at exit */
#define CLONE_VM        0x00000100      /* set if VM shared between processes */
#define CLONE_FS        0x00000200      /* set if fs info shared between process
es */
#define CLONE_FILES     0x00000400      /* set if open files shared between proc
esses */
#define CLONE_SIGHAND   0x00000800      /* set if signal handlers shared */
#define CLONE_PID       0x00001000      /* set if pid shared */

/*
 * These are the constant used to fake the fixed-point load-average
 * counting. Some notes:
 *  - 11 bit fractions expand to 22 bits by the multiplies: this gives
 *    a load-average precision of 10 bits integer + 11 bits fractional
 *  - if you want to count load-averages more often, you need more
 *    precision, or rounding will get you. With 2-second counting freq,
 *    the EXP_n values would be 1981, 2034 and 2043 if still using only
 *    11 bit fractions.
 */

extern unsigned long avenrun[];         /* Load averages */


#define FSHIFT          11              /* nr of bits of precision */
#define FIXED_1         (1<<FSHIFT)     /* 1.0 as fixed-point */
#define LOAD_FREQ       (5*HZ)          /* 5 sec intervals */
#define EXP_1           1884            /* 1/exp(5sec/1min) as fixed-point */
#define EXP_5           2014            /* 1/exp(5sec/5min) */
#define EXP_15          2037            /* 1/exp(5sec/15min) */

#define CALC_LOAD(load,exp,n) \
        load *= exp; \
        load += n*(FIXED_1-exp); \
        load >>= FSHIFT;

#define CT_TO_SECS(x)   ((x) / HZ)
#define CT_TO_USECS(x)  (((x) % HZ) * 1000000/HZ)


extern int nr_running, nr_tasks;

extern int nr_running;

#define FIRST_TASK task[0]
#define LAST_TASK task[NR_TASKS-1]


#include <linux/fs.h>
#include <linux/signal.h>
#include <linux/time.h>
#include <linux/resource.h>
#include <linux/ptrace.h>
#include <linux/timer.h>

#include <asm/processor.h>

#define TASK_RUNNING            0
#define TASK_INTERRUPTIBLE      1
#define TASK_UNINTERRUPTIBLE    2
#define TASK_ZOMBIE             3
#define TASK_STOPPED            4
#define TASK_SWAPPING           5

/*
 * Scheduling policies
 */
#define SCHED_OTHER             0
#define SCHED_FIFO              1
#define SCHED_RR                2

struct sched_param {
        int sched_priority;
};

/* Open file table structure */
struct files_struct {
        int count;
        fd_set close_on_exec; 
        fd_set open_fds;
        struct file * fd[NR_OPEN];
};

extern void sched_init(void);
extern void show_state(void);
extern void trap_init(void);

#define INIT_FILES { \
        1, \
        { { 0, } }, \
        { { 0, } }, \
        { NULL, } \
}

struct fs_struct {
        int count;
        unsigned short umask;
        struct inode * root, * pwd;
};

#define INIT_FS { \
        1, \
        0022, \
        NULL, NULL \
}



#ifndef NULL
#define NULL ((void *) 0)
#endif


struct mm_struct {
	int count;
        pgd_t * pgd;
        unsigned long context;
	unsigned long start_code, end_code, start_data, end_data;
	unsigned long start_brk, brk, start_stack, start_mmap;
        unsigned long arg_start, arg_end, env_start, env_end;
        unsigned long rss, total_vm, locked_vm;
        unsigned long def_flags;
        struct vm_area_struct * mmap;
        struct vm_area_struct * mmap_avl;

}; 

#define INIT_MM { \
                1, \
                swapper_pg_dir, \
                0, \
                0, 0, 0, 0, \
                0, 0, 0, 0, \
                0, 0, 0, 0, \
                0, 0, 0, \
                0, \
                &init_mmap, &init_mmap }

struct signal_struct {
        int count;
        struct sigaction action[32];
};

#define INIT_SIGNALS { \
                1, \
                { {0,}, } }

struct task_struct {
/* these are hardcoded - don't touch */
        volatile long state;    /* -1 unrunnable, 0 runnable, >0 stopped */
	long counter;
	long priority;
        unsigned long signal;
        unsigned long blocked;  /* bitmap of masked signals */
        unsigned long flags;    /* per process flags, defined below */

	struct exec_domain *exec_domain;
/* various fields */
        struct linux_binfmt *binfmt;
        unsigned long saved_kernel_stack;
        unsigned long kernel_stack_page;


	int exit_code, exit_signal;
	unsigned long personality;
        int did_exec:1;
	int dumpable:1;
	int pid;
	int pgrp;
        int tty_old_pgrp;
	int session;
        /* boolean value for session group leader */
        int leader;
        int     groups[NGROUPS];

        /*
         * pointers to (original) parent process, youngest child, younger sibling,
         * older sibling, respectively.  (p->father can be replaced with
         * p->p_pptr->pid)
         */
        struct task_struct *p_opptr, *p_pptr, *p_cptr, *p_ysptr, *p_osptr;
        struct wait_queue *wait_chldexit;       /* for wait4() */
        unsigned short uid,euid,suid,fsuid;
        unsigned short gid,egid,sgid,fsgid;
	unsigned long timeout, policy, rt_priority;
        unsigned long it_real_value, it_prof_value, it_virt_value;
        unsigned long it_real_incr, it_prof_incr, it_virt_incr;
        struct timer_list real_timer;
        unsigned long min_flt, maj_flt, nswap, cmin_flt, cmaj_flt, cnswap;
	int swappable:1;
        struct task_struct *next_task, *prev_task;
        struct task_struct *next_run,  *prev_run;
        long utime, stime, cutime, cstime, start_time;

/* limits */
        struct rlimit rlim[RLIM_NLIMITS];
        char comm[16];
/* file system info */
	int link_count;
        struct tty_struct *tty; /* NULL if no tty */
/* ipc stuff */
        struct sem_undo *semundo;
        struct sem_queue *semsleeping;
/* ldt for this task - used by Wine.  If NULL, default_ldt is used */
        struct desc_struct *ldt;
/* tss for this task */
        struct thread_struct tss;
/* filesystem information */
        struct fs_struct *fs;
/* open file information */
        struct files_struct *files;
/* memory management info */
        struct mm_struct *mm;
/* signal handlers */
        struct signal_struct *sig;
#if (SIX)
	int is_mapped;
	int user_mode;
	int kernel_level;
	/*
	 * user context and kernel context :
	 * a process can be in user land or in kernel land. the details
	 * of whatever it was doing in user land is stored in ucontext. in
	 * other words, when a process comes into kernel land, ucontext 
	 * gets updated with context info which can be later used to
	 * return to user land. now while in kernel land, a process can
	 * get scheduled out; while doing so, the context at that point gets
	 * stored to kcontext, so that it can be later scheduled in by
	 * restoring the context stored in kcontext. in brief, ucontext 
	 * is needed for returning to userland, and kcontext is needed for
	 * scheduling in a process.
	 *
	 *
	 *
	 *	          +-->--+      +-->-D . . . .E-->-+			
	 *	          |     |      |	          |	  		
	 *		  |     |      |	          |		kernel land
	 *	==========A=====B======C==================F====================
	 *		  |     |      |	          |		user land
	 *		  |     |      |	          |
	 *  	  -->-----+     +-->---+	          +----->----......
	 *
	 * Consider the flow of a process through userland, during which it
	 * keeps moving into and out of kernel land. point A denotes where
	 * it moves into kernel land for the first time; and the context at
	 * this point gets saved into ucontext. then into kernel land and
	 * at point B, ucontext(A) gets restored and the process lands back in
	 * user land. moving on, C denotes another entry into kernel land. 
	 * but this time, the process gets scheduled out. point D stands for
	 * this phenomenon, and the context at point D gets stored into kcontext.
	 * at a later point of time, point E, the process is scheduled back
	 * in; this is done by restoring kcontext(D). the process comes back
	 * into user land at point F, and this is achieved by restoring ucontext(C).
	 */
	struct pt_regs ucontext;
	struct pt_regs kcontext;
	int signum;
	/*
	 * Now what are these?
	 * To answer that, i should point out that there are 
	 * some system calls - like select() - which take more than
	 * three arguments. now we can use the ucontext_t structure 
	 * to pass three arguments at a time. How do we cope with
	 * a system call that takes, say, 6 arguments? The answer is,
	 * issue SIGLWP *twice*. First time: the three arguments that
	 * came in, are stored in the members which you see below. 
	 * second time: the remaining 3 arguments are collected, and
	 * along with the ones stored in one two and three, are used
	 * to fire the system call. yes i agree - sorry arsed setup.
	 */
	long one, two, three;
	/*
	 * And what abt this?
	 * this holds the address of the C library sigreturn() function.
	 *
	 * consider 2 processes, say A and B. suppose A comes up, and makes a
	 * call to signal(SIGUSR1, handler). the signal() library wrapper,
	 * while triggering the system call, passes along the address of
	 * the sigreturn() library function. when control eventually reaches
	 * the system call worker function, sys_signal(), it collects the
	 * address of the library sigreturn() and stores it here in this
	 * variable. and the address of the handler function gets stored
	 * in A's process table entry.
	 *
	 * now later on, B comes up, and decides to send a SIGUSR1 to A.
	 * to do this, B makes a call to kill(pid_of_A, SIGUSR1). which 
	 * leads to calls to kill_proc() -> send_sig() -> generate() etc
	 * and finally the signal mask of process A is set. and the state
	 * of A is ensured to be TASK_RUNNING so that it gets scheduled soon.
	 *
	 * so the scheduler soon finds A lying there, ready to fly, and
	 * gives it the CPU slot. suddenly A finds itself awake, and in the
	 * process of coming out of the schedule() system call, from the same
	 * point where it got scheduled out at an earlier point of time. it
	 * comes out of schedule(), finds itself back in ret_from_sys_call().
	 * now here, it checks its own signal mask; and finds that it is
	 * set, and proceeds to do_signal(). now a lot of messy work happens,
	 * which can be summarised as 5 steps :
	 *
	 * [1]
	 * first step is to have a backup of A's current context somewhere, so
	 * that we can come back later, once the signal business is over. the
	 * best place to do this is A'a stack itself. we have A's current stack
	 * pointer lying in context.uc_mcontext.gregs[17], so we grab it and
	 * (a) make room for a sigcontext structure 
	 * (b) copy the current context into that sigcontext structure
	 * (c) strech the stack further to leave room for a sparc stack frame (96 bytes)
	 * so now A's stack has a new sparc stack frame on top, followed by a
	 * sigcontext structure that holds the previous context information.
	 *
	 * [2]
	 * next, it should be ensured that the library wrapper should be called
	 * when A comes into user land. to do that, the value stored in this
	 * variable _sigreturn is stored in context.uc_mcontext.gregs[1], which
	 * denotes PC. and yes, context.uc_mcontext.gregs[2] is NPC, and it is
	 * set to PC + 4. 
	 * 
	 * [3]
	 * the sigcontext structure that we stored in A's stack should be accessed
	 * later on, so we need to remember its position in the stack. that is
	 * achieved by storing its stack address in register G6. corresponding
	 * entry in the context structure is context.uc_mcontext.gregs[19].
	 *
	 * [4]
	 * Obviously the C library routine sigreturn() is going to execute when
	 * A comes into user land; and its the duty of sigreturn() to call the
	 * handler specified earlier by A, via the call to signal(). so we should
	 * make the value of handler available to sigreturn(). this is achieved
	 * by storing taking the value of handler from A's process table entry 
	 * and storing it in register G7.
	 * 
	 * All this done, handle_signal() etc returns, back into ret_from_sys_call,
	 * and then onto sun_handler(), and finally into a getcontext() which
	 * brings A back into userland. now sigreturn() executes, since thats what
	 * the PC is set to. sigreturn does the following :
	 *
	 * [1] finds the value of the actual handler from register G7 and calls it.
	 * 
	 * [2] so now the system call handling has taken place and its time to
	 * clean up. first, the address of the sigcontext structure lying on the stack
	 * is obtained from register G6. next, the sigreturn *system call* is triggered.
	 * and the address of the sigcontext structure is passed as argument.
	 *
	 * [3] the sigreturn system call takes the sigcontext structure, finds in it
	 * the old context information, and replaces the current context information
	 * with the old one. 
	 * 
	 * so when A is brought into userland after the sigreturn system call, the
	 * context that gets restored is the old one, which stands for what A was
	 * doing when B made the kill(). so A happily returns to that point and
	 * resumes whatever it was doing.
	 */
	unsigned int _sigreturn;
	/*
	 * Now what the fuck is this?
	 * Ok basically more shit. consider a process running in user land.
	 * now say some damn interrupt comes in, say the timer, or say someone
	 * fingered the keyboard, something like that. whatever, we reach
	 * the generic interrupt handler, sun_handler(). so we are now into
	 * kernel land, and it is no longer a wise idea to keep sitting on
	 * the user stack. we need to switch over to kernel stack, and to
	 * do that, we need to save the user stack pointer somewhere, so that
	 * we can restore it later, when we leave kernel land. and thats 
	 * why we have the member 'ost' here. 
	 */
	long osp, nsp;
#if (__i386__)
	long obp;
#else
	long dummy; /* for now! */
#endif
#endif
#ifdef __SMP__
#if (!SIX)
        int processor;
        int last_processor;
        int lock_depth;         /* Lock depth. We can context switch in and out
				   of holding a syscall kernel
 				   lock... */
#endif
#endif
};


#define PF_PTRACED      0x00000010      /* set if ptrace (0) has been called. */
#define PF_TRACESYS     0x00000020      /* tracing system calls */
#define PF_FORKNOEXEC   0x00000040      /* forked but didn't exec */
#define PF_SUPERPRIV    0x00000100      /* used super-user privileges */
#define PF_DUMPCORE     0x00000200      /* dumped core */
#define PF_SIGNALED     0x00000400      /* killed by a signal */

#define PF_STARTING     0x00000100      /* being created */
#define PF_EXITING      0x00000200      /* getting shut down */

#define PF_USEDFPU      0x00100000     /* Process used the FPU this quantum (SMP only) */

#define PF_DTRACE       0x00200000      /* delayed trace (used on m68k) */

/*
 * Limit the stack by to some sane default: root can always
 * increase this limit if needed..  8MB seems reasonable.
 */
#define _STK_LIM        (8*1024*1024)


#define DEF_PRIORITY    1 //(20*HZ/100)     /* 200 ms time slices */

/*
 *  INIT_TASK is used to set up the first task table, touch at
 * your own risk!. Base=0, limit=0x1fffff (=2MB)
 */

#define INIT_TASK  {\
/* state */		0, \
/* counter */		DEF_PRIORITY, \
/* priority */		DEF_PRIORITY, \
/* signal */		0, \
/* blocked */		0, \
/* flags */		0, \
/* exec domain */	&default_exec_domain, \
/* binfmt */		0, \
/* saved stack */	0, \
/* init stack */	&init_kernel_stack, \
/* exit_code */		0, \
/* personality */	0, \
/* exit_signal */	0, \
/* did_exec */		0, \
/* dumpable */		0, \
/* pid */		0, \
/* pgrp */		0, \
/* tty_old_pgrp */	0, \
/* session */		0, \
/* leader */		0, \
/* groups */		{NOGROUP,}, \
/* p_opptr */		0, \
/* p_pptr */		0, \
/* p_cptr */		0, \
/* p_ysptr */		0, \
/* p_osptr */		0, \
/* wait_chldexit */	0, \
/* uid */		0, \
/* euid */		0, \
/* suid */		0, \
/* fsuid */		0, \
/* gid */		0, \
/* egid */		0, \
/* sgid */		0, \
/* fsgid */		0, \
/* timeout */		0, \
/* policy */		2, \
/* rt_priority */	0, \
/* it_real_value */	0, \
/* it_prof_value */	0, \
/* it_virt_value */	0, \
/* it_real_incr */	0, \
/* it_prof_incr */	0, \
/* it_virt_incr */ 	0, \
/* real_timer */	{0, 0, 0, 0, it_real_fn }, \
/* min_flt */		0, \
/* maj_flt */		0, \
/* nswap */		0, \
/* cmin_flt */		0, \
/* cmaj_flt */		0, \
/* cnswap */		0, \
/* swappable */		0, \
/* next_task */		&init_task, \
/* prev_task */		&init_task, \
/* next_run */		&init_task, \
/* prev_run */		&init_task, \
/* utime */		0, \
/* stime */		0, \
/* cutime */		0, \
/* cstime */		0, \
/* start_time */	0, \
/* rlim */		INIT_RLIMITS, \
/* comm */		"swapper", \
/* link count */	0, \
/* tty */		0, \
/* semundo */		0, \
/*semqueue */		0, \
/* ldt */		0, \
/* tss */		INIT_TSS, \
/* fs */		&init_fs, \
/* files */		&init_files, \
/* mm */	        &init_mm, \
/* signals */		&init_signals, \
/* is_mapped */		0, \
/* user mode */		0, \
/* kernel level */	0, \
/* ucontext */		0, \
/* kcontext */		0, \
/* signum */		0, \
/* one	*/		0, \
/* two	*/		0, \
/* three */		0, \
/* _sigreturn */	0, \
/* osp	*/		0, \
/* nsp	*/		0, \
/* dummy or obp	*/	0 \
}

extern unsigned long volatile jiffies;

extern struct timeval xtime;
extern int need_resched;
extern void do_timer(struct pt_regs *);

extern unsigned int * prof_buffer;
extern unsigned long prof_len;
extern unsigned long prof_shift;

extern int securelevel; /* system security level */

#define CURRENT_TIME (xtime.tv_sec)


extern struct task_struct *current_set[NR_CPUS];
/*
 *      On a single processor system this comes out as current_set[0] when cpp
 *      has finished with it, which gcc will optimise away.
 */
#define current (0+current_set[smp_processor_id()])     /* Current on this processor */

#if (SIX)
extern struct  task_struct *mapped_proc;
#endif

extern struct timeval xtime;

extern struct   mm_struct init_mm;
extern struct task_struct *task[NR_TASKS];
extern struct task_struct init_task;
extern struct task_struct *last_task_used_math;

#define for_each_task(p) \
        for (p = &init_task ; (p = p->next_task) != &init_task ; )

extern int do_fork(unsigned long, unsigned long, struct pt_regs *);

/*
 * The wait-queues are circular lists, and you have to be *very* sure
 * to keep them correct. Use only these two functions to add/remove
 * entries in the queues.
 */
#if (SIX)
static inline void add_wait_queue(struct wait_queue ** p, struct wait_queue * wait)
#else
extern inline void add_wait_queue(struct wait_queue ** p, struct wait_queue * wait)
#endif
{
        unsigned long flags;

#ifdef DEBUG
        if (wait->next) {
                __label__ here;
                unsigned long pc;
                pc = (unsigned long) &&here;
              here:
                printk("add_wait_queue (%08lx): wait->next = %08lx\n",pc,(unsigned long) wait->next);
        }
#endif
        save_flags(flags);
        cli();
        if (!*p) {
                wait->next = wait;
                *p = wait;
        } else {
                wait->next = (*p)->next;
                (*p)->next = wait;
        }
        restore_flags(flags);
}


#if (SIX)
static inline void remove_wait_queue(struct wait_queue ** p, struct wait_queue * wait)
#else
extern inline void remove_wait_queue(struct wait_queue ** p, struct wait_queue * wait)
#endif
{
        unsigned long flags;
        struct wait_queue * tmp;
#ifdef DEBUG
        unsigned long ok = 0;
#endif

        save_flags(flags);
        cli();
        if ((*p == wait) &&
#ifdef DEBUG
            (ok = 1) &&
#endif
            ((*p = wait->next) == wait)) {
                *p = NULL;
        } else {
                tmp = wait;
                while (tmp->next != wait) {
                        tmp = tmp->next;
#ifdef DEBUG
                        if (tmp == *p)
                                ok = 1;
#endif
                }
                tmp->next = wait->next;
        }
        wait->next = NULL;
        restore_flags(flags);
#ifdef DEBUG
        if (!ok) {
                __label__ here;
                ok = (unsigned long) &&here;
                printk("removed wait_queue not on list.\n");
                printk("list = %08lx, queue = %08lx\n",(unsigned long) p, (unsigned long) wait);
              here:
                printk("eip = %08lx\n",ok);
        }
#endif
}

extern void __down(struct semaphore * sem);

#if (SIX)
static inline void select_wait(struct wait_queue ** wait_address, select_table * p)
#else
extern inline void select_wait(struct wait_queue ** wait_address, select_table * p)
#endif
{
        struct select_table_entry * entry;

        if (!p || !wait_address)
                return;
        if (p->nr >= __MAX_SELECT_TABLE_ENTRIES)
                return;
        entry = p->entry + p->nr;
        entry->wait_address = wait_address;
        entry->wait.task = current;
        entry->wait.next = NULL;
        add_wait_queue(wait_address,&entry->wait);
        p->nr++;
}

/*              
 * These are not yet interrupt-safe
 */     
#if (SIX)
static inline void down(struct semaphore * sem)
#else
extern inline void down(struct semaphore * sem)
#endif
{       
        if (sem->count <= 0)               
                __down(sem);            
        sem->count--;
}                                          


#if (SIX)
static inline void up(struct semaphore * sem)
#else
extern inline void up(struct semaphore * sem)
#endif
{       
        sem->count++;
        wake_up(&sem->wait);
}   

#define REMOVE_LINKS(p) do { unsigned long flags; \
        save_flags(flags) ; cli(); \
        (p)->next_task->prev_task = (p)->prev_task; \
        (p)->prev_task->next_task = (p)->next_task; \
        restore_flags(flags); \
        if ((p)->p_osptr) \
                (p)->p_osptr->p_ysptr = (p)->p_ysptr; \
        if ((p)->p_ysptr) \
                (p)->p_ysptr->p_osptr = (p)->p_osptr; \
        else \
                (p)->p_pptr->p_cptr = (p)->p_osptr; \
        } while (0)


#define SET_LINKS(p) do { unsigned long flags; \
        save_flags(flags); cli(); \
        (p)->next_task = &init_task; \
        (p)->prev_task = init_task.prev_task; \
        init_task.prev_task->next_task = (p); \
        init_task.prev_task = (p); \
        restore_flags(flags); \
        (p)->p_ysptr = NULL; \
        if (((p)->p_osptr = (p)->p_pptr->p_cptr) != NULL) \
                (p)->p_osptr->p_ysptr = p; \
        (p)->p_pptr->p_cptr = p; \
        } while (0)


#endif
