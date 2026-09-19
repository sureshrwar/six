
#ifndef _LINUX_KERNEL_H
#define _LINUX_KERNEL_H

/*
 * 'kernel.h' contains some often-used function prototypes etc
 */

#ifdef __KERNEL__

/* Optimization barrier */
#if (SIX)
#define barrier() __asm__("": : :"memory")
#else
static inline void barrier()  {
	/* nothing */
}
#endif

#if (!SIX)
#define INT_MAX         ((int)(~0U>>1))
#define UINT_MAX        (~0U)
#define LONG_MAX        ((long)(~0UL>>1))
#define ULONG_MAX       (~0UL)
#else
/*
 * we have these in linux/limits.h.
 */
#endif

#define STACK_MAGIC     0xdeadbeef

#define KERN_EMERG      "<0>"   /* system is unusable                   */
#define KERN_ALERT      "<1>"   /* action must be taken immediately     */
#define KERN_CRIT       "<2>"   /* critical conditions                  */
#define KERN_ERR        "<3>"   /* error conditions                     */
#define KERN_WARNING    "<4>"   /* warning conditions                   */
#define KERN_NOTICE     "<5>"   /* normal but significant condition     */
#define KERN_INFO       "<6>"   /* informational                        */
#define KERN_DEBUG      "<7>"   /* debug-level messages                 */

# define NORET_TYPE    /**/

/*                              
 * "suser()" checks against the effective user id, while "fsuser()"
 * is used for file permission checking and checks against the fsuid..
 */                                     
#define fsuser() (current->fsuid == 0)

#endif /* __KERNEL__ */

#define SI_LOAD_SHIFT   16
struct sysinfo {
        long uptime;                    /* Seconds since boot */
        unsigned long loads[3];         /* 1, 5, and 15 minute load averages */
        unsigned long totalram;         /* Total usable main memory size */
        unsigned long freeram;          /* Available memory size */
        unsigned long sharedram;        /* Amount of shared memory */
        unsigned long bufferram;        /* Memory used by buffers */
        unsigned long totalswap;        /* Total swap space size */
        unsigned long freeswap;         /* swap space still available */
        unsigned short procs;           /* Number of current processes */
        char _f[22];                    /* Pads structure to 64 bytes */
};


#endif
