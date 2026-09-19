
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



/*
 * Under SIX an "IRQ" is a host signal, and the IRQ number *is* the signal
 * number: init_IRQ() calls setup_x86_irq(SIX_HOST_TRAPSIG, ...) and
 * do_IRQ() is handed the signal it was delivered.
 *
 * That makes NR_IRQS a property of the host, not of the PC interrupt
 * controller.  It was 32, which was fine while the trap was Solaris
 * SIGLWP (33 -- already one over, in fact), but Linux reserves 32 and 33
 * for NPTL so the trap had to move to SIGRTMIN+4 == 38.  Every
 * irq_action[38] and kstat.interrupts[38] then wrote past the end of its
 * array; the observed symptom was timer_active silently acquiring bit 2
 * (RS_TIMER), whose handler is NULL because the serial driver is not even
 * compiled, and the kernel jumping to address 0 on the next tick.
 *
 * Linux signals run to SIGRTMAX == 64, so size for 65 entries and be done.
 */
#define NR_IRQS 65

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


#define SAVE_ALL			do { \
						memcpy(&current->ucontext, \
						       context, sizeof(struct pt_regs)); \
						six_fix_context(&current->ucontext); \
					} while (0)
#define RESTORE_USER_CONTEXT		setcontext(&current->ucontext)
#define RESTORE_CONTEXT			setcontext(context)
#define ENTER_KERNEL			kernel_counter++;  \
					current->kernel_level++
#define LEAVE_KERNEL			kernel_counter--; \
					current->kernel_level--

#endif


