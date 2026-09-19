
/*
 *  linux/mm/vmscan.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Swap reorganised 29.12.95, Stephen Tweedie.
 *  kswapd added: 7.1.96  sct
 *  Version: $Id: vmscan.c,v 1.1.1.1 2005/03/30 08:40:50 motorbreathing Exp $
 */

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/swap.h>
#include <linux/fs.h>
#include <linux/swapctl.h>
#include <linux/smp_lock.h>

#include <asm/dma.h>
#include <asm/system.h> /* for cli()/sti() */
#include <asm/segment.h> /* for memcpy_to/fromfs */
#include <asm/bitops.h>
#include <asm/pgtable.h>

/*
 * When are we next due for a page scan?
 */
static int next_swap_jiffies = 0;

/*
 * How often do we do a pageout scan during normal conditions?
 * Default is four times a second.
 */
int swapout_interval = HZ / 4;

/*
 * The wait queue for waking up the pageout daemon:
 */
static struct wait_queue * kswapd_wait = NULL;

/*
 * We avoid doing a reschedule if the pageout daemon is already awake;
 */
static int kswapd_awake = 0;


/*
 * sysctl-modifiable parameters to control the aggressiveness of the
 * page-searching within the kswapd page recovery daemon.
 */
kswapd_control_t kswapd_ctl = {4, -1, -1, -1, -1};


/*
 * We are much more aggressive about trying to swap out than we used
 * to be.  This works out OK, because we now do proper aging on page
 * contents.
 */
int try_to_free_page(int priority, int dma, int wait)
{
        static int state = 0;
        int i=6;
        int stop;

        /* we don't try as hard if we're not waiting.. */
        stop = 3;
        if (wait)
                stop = 0;
        switch (state) {
                do {
                case 0:
                        if (shrink_mmap(i, dma))
                                return 1;
                        state = 1;
                case 1:
                        if (shm_swap(i, dma))
                                return 1;
                        state = 2;
                default:
                        if (swap_out(i, dma, wait))
                                return 1;
                        state = 0;
                i--;
                } while ((i - stop) >= 0);
        }
        return 0;
}



static int swap_out(unsigned int priority, int dma, int wait)
{

	return 1;

}


/*
 * The background pageout daemon.
 * Started as a kernel thread from the init process.
 */
int kswapd(void *unused)
{
        int i;
        char *revision="$Revision: 1.1.1.1 $", *s, *e;

        current->session = 1;
        current->pgrp = 1;
        sprintf(current->comm, "kswapd");
        current->blocked = ~0UL;

        /*
         *      As a kernel thread we want to tamper with system buffers
         *      and other internals and thus be subject to the SMP locking
         *      rules. (On a uniprocessor box this does nothing).
         */

#ifdef __SMP__
        lock_kernel();
        syscall_count++;
#endif

        /* Give kswapd a realtime priority. */
        current->policy = SCHED_FIFO;
        current->priority = 32;  /* Fixme --- we need to standardise our
                                    namings for POSIX.4 realtime scheduling
                                    priorities.  */

        init_swap_timer();

        if ((s = strchr(revision, ':')) &&
            (e = strchr(s, '$')))
                s++, i = e - s;
        else
                s = revision, i = -1;
        printk ("Started kswapd v%.*s\n", i, s);

        while (1) {
                kswapd_awake = 0;
                current->signal = 0;
                run_task_queue(&tq_disk);
                interruptible_sleep_on(&kswapd_wait);
                kswapd_awake = 1;
                swapstats.wakeups++;
                /* Do the background pageout: */
                for (i=0; i < kswapd_ctl.maxpages; i++)
                        try_to_free_page(GFP_KERNEL, 0, 0);
        }
}

/*
 * The swap_tick function gets called on every clock tick.
 */

void swap_tick(void)
{
        if ((nr_free_pages + nr_async_pages) < free_pages_low ||
            ((nr_free_pages + nr_async_pages) < free_pages_high &&
             jiffies >= next_swap_jiffies)) {
                if (!kswapd_awake && kswapd_ctl.maxpages > 0) {
                        wake_up(&kswapd_wait);
                        need_resched = 1;
                        kswapd_awake = 1;
                }
                next_swap_jiffies = jiffies + swapout_interval;
        }
        timer_active |= (1<<SWAP_TIMER);
}



/*
 * Initialise the swap timer
 */

void init_swap_timer(void)
{
        timer_table[SWAP_TIMER].expires = 0;
        timer_table[SWAP_TIMER].fn = swap_tick;
        timer_active |= (1<<SWAP_TIMER);
}

