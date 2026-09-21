
/*
 *  linux/kernel/panic.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * This function is used through-out the kernel (including mm and fs)
 * to indicate a major problem.
 */
#include <stdarg.h>

#include <solaris.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/sysctl.h>
#include <asm/system.h>



asmlinkage void sys_sync(void); /* it's really int */
extern void hard_reset_now(void);
extern void do_unblank_screen(void);
extern void dump_stack(void);
extern void show_state(void);
extern void show_mem(void);
extern void show_timers(void);
extern void show_locks(void);
extern void show_ftrace(void);
extern void show_all_cpu_bt(void);
extern int oops_in_progress;
extern int C_A_D;

int panic_timeout = 0;
unsigned long panic_print = 0x3fUL; /* Default: all 6 diagnostic sections */

static void panic_print_sys_info(void)
{
        if (panic_print & PANIC_PRINT_ALL_CPU_BT)
                show_all_cpu_bt();
        if (panic_print & PANIC_PRINT_TASK_INFO)
                show_state();
        if (panic_print & PANIC_PRINT_MEM_INFO)
                show_mem();
        if (panic_print & PANIC_PRINT_TIMER_INFO)
                show_timers();
        if (panic_print & PANIC_PRINT_LOCK_INFO)
                show_locks();
        if (panic_print & PANIC_PRINT_FTRACE_INFO)
                show_ftrace();
}

NORET_TYPE void panic(const char * fmt, ...)
{
        static char buf[1024];
        static int in_panic = 0;
        va_list args;
        int i;

        cli();
        if (in_panic++) {
                hard_reset_now();
        }

        va_start(args, fmt);
        vsprintf(buf, fmt, args);
        va_end(args);
        printk(KERN_EMERG "\nKernel panic: %s\n", buf);
#if (SIX)
        if (!oops_in_progress)
                dump_stack();
        panic_print_sys_info();
#endif
        if (!current || current == task[0])
                printk(KERN_EMERG "In swapper task - not syncing\n");
        else
                sys_sync();

        do_unblank_screen();

        if (panic_timeout > 0)
        {
                /*
                 * Delay timeout seconds before rebooting the machine.
                 * We can't use the "normal" timers since we just panicked..
                 */
                printk(KERN_EMERG "Rebooting in %d seconds..",panic_timeout);
                for(i = 0; i < (panic_timeout*1000); i++)
                        udelay(1000);
                hard_reset_now();
        }
#if (SIX)
        printk(KERN_EMERG "System halted.\n");
        hard_reset_now();
#endif
        for(;;);
}


/*
 * GCC 2.5.8 doesn't always optimize correctly; see include/asm/segment.h
 */

int bad_user_access_length(void)
{
	panic("bad_user_access_length executed (not cool, dude)");
}



