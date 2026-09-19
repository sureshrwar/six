#ifndef _SCHED_H
#define _SCHED_H

/*
 * The scheduler interface, for guest programs.
 *
 * library/sys/sched_*.c used to include <linux/sched.h> to get these four
 * declarations.  That is a kernel-internal header: it pulls in tty.h,
 * fs.h, quota.h and mount.h, and those use kdev_t, which only exists when
 * __KERNEL__ is defined.  The guest build got away with it in 2005 only
 * because it was also passing -D__KERNEL__, which is not a thing a user
 * program should ever do -- among other effects it changes the visible
 * definitions in include/linux/types.h.
 *
 * So the four things the guest actually needs live here instead.  They are
 * ABI, and must agree with include/linux/sched.h.
 */

#include <linux/types.h>

/*
 * Scheduling policies.  Must match include/linux/sched.h.
 */
#define SCHED_OTHER             0
#define SCHED_FIFO              1
#define SCHED_RR                2

struct sched_param {
        int sched_priority;
};

extern int sched_setparam(pid_t p, struct sched_param *param);
extern int sched_getparam(pid_t p, struct sched_param *param);
extern int sched_setscheduler(pid_t p, int policy, struct sched_param *param);
extern int sched_getscheduler(pid_t p);
extern int sched_yield(void);
extern int sched_get_priority_max(int policy);
extern int sched_get_priority_min(int policy);

#endif /* _SCHED_H */
