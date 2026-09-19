
#ifndef __ASM_SYSTEM_H
#define __ASM_SYSTEM_H

#include <asm/segment.h>
#include <linux/signal.h>

/*
 * Entry into gdt where to find first TSS. GDT layout:
 *   0 - null
 *   1 - not used
 *   2 - kernel code segment
 *   3 - kernel data segment
 *   4 - user code segment
 *   5 - user data segment
 * ...
 *   8 - TSS #0
 *   9 - LDT #0
 *  10 - TSS #1
 *  11 - LDT #1
 */
#define FIRST_TSS_ENTRY 8
#define FIRST_LDT_ENTRY (FIRST_TSS_ENTRY+1)
#define _TSS(n) ((((unsigned long) n)<<4)+(FIRST_TSS_ENTRY<<3))
#define _LDT(n) ((((unsigned long) n)<<4)+(FIRST_LDT_ENTRY<<3))
#define load_TR(n)
#define load_ldt(n)

#define KERNEL_THREAD_REQUEST	666

#define switch_to(prev, next)	set_proc_mappings(); \
				swapcontext(&prev->kcontext, &next->kcontext)

#include <asm/signal.h>

extern so_sigset_t uni_lock;
extern so_sigset_t uni_unlock;

/*
 * Defined in arch/six/kernel/host.c.  Declared by hand rather than by
 * including host.h so that this header stays self-contained.
 *
 * These used to call sigprocmask() directly with a so_sigset_t, which is
 * Solaris-shaped (16 bytes).  glibc's sigset_t is 128 bytes, so that
 * scribbled past the end of the caller's object.
 */
extern void six_host_cli(void);
extern void six_host_sti(void);

static inline lock()
{
	six_host_cli();
}

static inline unlock()
{
	six_host_sti();
}

#define sti()	unlock()
#define cli()   lock()
#define save_flags
#define restore_flags(f) unlock() 

#define _set_gate(gate_addr,type,dpl,addr)

#define set_intr_gate(n,addr)

#define set_trap_gate(n,addr)

#define set_system_gate(n,addr)

#define set_call_gate(a,addr)

#define set_tss_desc(n,addr)

#define set_ldt_desc(n,addr,size)

#define clts()

#define stts() 

static inline int xchg(long *ptr, long val)
{
	long old;
	old = *ptr;
	*ptr = val;
	return old;
}

/*
 * Trap into the kernel.
 *
 * This was raise(SIGLWP), I.e. Solaris signal 33.  Linux/NPTL reserves 32
 * and 33 for the threading library and sigaction() refuses to install a
 * handler for them, so the trap moved to SIGRTMIN+4 -- but this macro was
 * left pointing at the old number, with the result that every syscall
 * raised a signal nobody was listening for and system_call() was never
 * entered.  Go through the host shim so there is exactly one definition of
 * which signal the trap actually is.
 */
extern void six_host_raise_trap(void);

#define  INT_SYSCALL	six_host_raise_trap()

#endif
