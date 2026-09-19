
#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

/*
 *      linux/include/asm/irq.h
 *
 *      (C) 1992, 1993 Linus Torvalds
 *
 *      IRQ/IPI changes taken from work by Thomas Radke <tomsoft@informatik.tu-c
hemnitz.de>
 */



#define NR_IRQS 32

#define TIMER_IRQ 0

#define IRQ0_interrupt  0
#define IRQ1_interrupt	0
#define IRQ2_interrupt	0
#define IRQ3_interrupt	0
#define IRQ4_interrupt	0
#define IRQ5_interrupt	0
#define IRQ6_interrupt	0
#define IRQ7_interrupt	0
#define IRQ8_interrupt	0
#define IRQ9_interrupt	0
#define IRQ10_interrupt	0
#define IRQ11_interrupt	0
#define IRQ12_interrupt	0
#define IRQ13_interrupt	0
#define IRQ14_interrupt	0
#define IRQ15_interrupt	0
#define IRQ16_interrupt	0


#define fast_IRQ0_interrupt	0
#define fast_IRQ1_interrupt	0
#define fast_IRQ2_interrupt	0
#define fast_IRQ3_interrupt	0
#define fast_IRQ4_interrupt	0
#define fast_IRQ5_interrupt	0
#define fast_IRQ6_interrupt	0
#define fast_IRQ7_interrupt	0
#define fast_IRQ8_interrupt	0
#define fast_IRQ9_interrupt	0
#define fast_IRQ10_interrupt	0
#define fast_IRQ11_interrupt	0
#define fast_IRQ12_interrupt	0
#define fast_IRQ13_interrupt	0
#define fast_IRQ14_interrupt	0
#define fast_IRQ15_interrupt	0
#define fast_IRQ16_interrupt	0


#define bad_IRQ0_interrupt	0
#define bad_IRQ1_interrupt	0
#define bad_IRQ2_interrupt	0
#define bad_IRQ3_interrupt	0
#define bad_IRQ4_interrupt	0
#define bad_IRQ5_interrupt	0
#define bad_IRQ6_interrupt	0
#define bad_IRQ7_interrupt	0
#define bad_IRQ8_interrupt	0
#define bad_IRQ9_interrupt	0
#define bad_IRQ10_interrupt	0
#define bad_IRQ11_interrupt	0
#define bad_IRQ12_interrupt	0
#define bad_IRQ13_interrupt	0
#define bad_IRQ14_interrupt	0
#define bad_IRQ15_interrupt	0
#define bad_IRQ16_interrupt	0


#define SAVE_ALL			memcpy(&current->ucontext, \
					context, sizeof(struct pt_regs)) 
#define RESTORE_USER_CONTEXT		setcontext(&current->ucontext)
#define RESTORE_CONTEXT			setcontext(context)
#define ENTER_KERNEL			kernel_counter++;  \
					current->kernel_level++
#define LEAVE_KERNEL			kernel_counter--; \
					current->kernel_level--

#endif


