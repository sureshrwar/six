
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
extern int kmsg_get_snapshot(char *dst, int maxlen);
extern int ftrace_get_snapshot(char *dst, int maxlen);
asmlinkage int sys_open(const char *, int, int);
asmlinkage int sys_write(unsigned int, const char *, int);
asmlinkage int sys_close(unsigned int);
extern int oops_in_progress;
extern int C_A_D;

int panic_timeout = 0;
unsigned long panic_print = 0UL;

#if (SIX)
static char pstore_kmsg_buf[16384];
static char pstore_ftrace_buf[2048];

static void pstore_dump(void)
{
        int klen, flen, fd;
        unsigned short old_euid, old_fsuid;

        if (!current || current == task[0] || !current->files)
                return;

        klen = kmsg_get_snapshot(pstore_kmsg_buf, sizeof(pstore_kmsg_buf));
        flen = ftrace_get_snapshot(pstore_ftrace_buf, sizeof(pstore_ftrace_buf));

        old_euid = current->euid;
        old_fsuid = current->fsuid;
        current->euid = 0;
        current->fsuid = 0;

        /* O_WRONLY | O_CREAT | O_TRUNC == 01101 */
        fd = sys_open("/sys/fs/pstore/dmesg-ramoops-0", 01101, 0644);
        if (fd >= 0) {
                sys_write(fd, pstore_kmsg_buf, klen);
                sys_close(fd);
        }

        if (flen > 0) {
                fd = sys_open("/sys/fs/pstore/ftrace-ramoops-0", 01101, 0644);
                if (fd >= 0) {
                        sys_write(fd, pstore_ftrace_buf, flen);
                        sys_close(fd);
                }
        }

        current->euid = old_euid;
        current->fsuid = old_fsuid;
        printk(KERN_EMERG "pstore: saved crash dump to /sys/fs/pstore/dmesg-ramoops-0 (%d bytes)\n", klen);
}
#endif

static unsigned long sys_info_already_dumped = 0;

void kernel_sys_info(unsigned long mask)
{
        unsigned long todo = mask & ~sys_info_already_dumped;
        sys_info_already_dumped |= todo;

        if (todo & PANIC_PRINT_ALL_CPU_BT)
                show_all_cpu_bt();
        if (todo & PANIC_PRINT_TASK_INFO)
                show_state();
        if (todo & PANIC_PRINT_MEM_INFO)
                show_mem();
        if (todo & PANIC_PRINT_TIMER_INFO)
                show_timers();
        if (todo & PANIC_PRINT_LOCK_INFO)
                show_locks();
        if (todo & PANIC_PRINT_FTRACE_INFO)
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
        kernel_sys_info(panic_print);
#endif
        if (!current || current == task[0])
                printk(KERN_EMERG "In swapper task - not syncing\n");
        else {
#if (SIX)
                pstore_dump();
#endif
                sys_sync();
        }

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



