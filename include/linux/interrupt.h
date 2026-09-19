
/* interrupt.h */
#ifndef _LINUX_INTERRUPT_H
#define _LINUX_INTERRUPT_H

#include <asm/bitops.h>



struct irqaction {
        void (*handler)(int, void *, struct pt_regs *);
        unsigned long flags;
        unsigned long mask;
        const char *name;
        void *dev_id;
        struct irqaction *next;
};

extern unsigned long intr_count;


extern int bh_mask_count[32];
extern unsigned long bh_active;
extern unsigned long bh_mask;
extern void (*bh_base[32])(void);



/* Who gets which entry in bh_base.  Things which will occur most often
   should come first - in which case NET should be up the top with SERIAL/TQUEUE! */

enum {
        TIMER_BH = 0,
        CONSOLE_BH,
        TQUEUE_BH,
        DIGI_BH,
        SERIAL_BH,
        RISCOM8_BH,
        BAYCOM_BH,
        NET_BH,
        IMMEDIATE_BH,
        KEYBOARD_BH,
        CYCLADES_BH,
        CM206_BH
};

extern inline void mark_bh(int nr)
{
        set_bit(nr, &bh_active);
}

/* 
 * These use a mask count to correctly handle
 * nested disable/enable calls
 */
#if (SIX)
static inline void disable_bh(int nr)
#else
extern inline void disable_bh(int nr)
#endif
{       
        bh_mask &= ~(1 << nr);
        bh_mask_count[nr]++;
}       
        
#if (SIX)
static inline void enable_bh(int nr)
#else
extern inline void enable_bh(int nr)
#endif
{       
        if (!--bh_mask_count[nr])
                bh_mask |= 1 << nr;
}       

/* 
 * start_bh_atomic/end_bh_atomic also nest
 * naturally by using a counter
 */
#if (SIX)
static inline void start_bh_atomic(void)
#else
extern inline void start_bh_atomic(void)
#endif
{
        intr_count++; 
        barrier();
}
        
#if (SIX)
static inline void end_bh_atomic(void)
#else
extern inline void end_bh_atomic(void)
#endif
{       
        barrier();
        intr_count--;
}               

#endif
