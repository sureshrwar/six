
/*
 *  linux/arch/i386/traps.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * 'Traps.c' handles hardware traps and faults after we have saved some
 * state in 'asm.s'. Currently mostly a debugging-aid, will be extended
 * to mainly kill the offending process (probably by giving it a signal,
 * but possibly by killing it outright if necessary).
 */


#include <linux/head.h>
#include <linux/sched.h>

#if (SIX)
desc_table idt,gdt;
#endif

#include <asm/system.h>


/*
 * Note that we play around with the 'TS' bit to hopefully get
 * the correct behaviour even in the presence of the asynchronous
 * IRQ13 behaviour
 */
void math_error(void)
{
        struct task_struct * task;

        clts();
#ifdef __SMP__
        task = current;
#else
        task = last_task_used_math;
        last_task_used_math = NULL;
        if (!task) {
#if (!SIX)
                __asm__("fnclex");
#endif
                return;
        }
#endif
        /*
         *      Save the info for the exception handler
         */
#if (!SIX)
        __asm__ __volatile__("fnsave %0":"=m" (task->tss.i387.hard));
#endif
        task->flags&=~PF_USEDFPU;
        stts();

        force_sig(SIGFPE, task);
        task->tss.trap_no = 16;
        task->tss.error_code = 0;
}



void trap_init(void)
{
        int i;
        struct desc_struct * p;
        static int smptrap=0;

        if(smptrap)
        {
#if (!SIX)
                __asm__("pushfl ; andl $0xffffbfff,(%esp) ; popfl");
#endif
                load_ldt(0);
                return;
        }
        smptrap++;
#if (!SIX)
        if (strncmp((char*)0x0FFFD9, "EISA", 4) == 0)
                EISA_bus = 1;
#endif
        set_call_gate(&default_ldt,lcall7);
        set_trap_gate(0,&divide_error);
        set_trap_gate(1,&debug);
        set_trap_gate(2,&nmi);
        set_system_gate(3,&int3);       /* int3-5 can be called from all */
        set_system_gate(4,&overflow);
        set_system_gate(5,&bounds);
        set_trap_gate(6,&invalid_op);
        set_trap_gate(7,&device_not_available);
        set_trap_gate(8,&double_fault);
        set_trap_gate(9,&coprocessor_segment_overrun);
        set_trap_gate(10,&invalid_TSS);
        set_trap_gate(11,&segment_not_present);
        set_trap_gate(12,&stack_segment);
        set_trap_gate(13,&general_protection);
        set_trap_gate(14,&page_fault);
        set_trap_gate(15,&reserved);
        set_trap_gate(16,&coprocessor_error);
        set_trap_gate(17,&alignment_check);
        for (i=18;i<48;i++)
                set_trap_gate(i,&reserved);
        set_system_gate(0x80,&system_call);
/* set up GDT task & ldt entries */
        p = gdt+FIRST_TSS_ENTRY;
        set_tss_desc(p, &init_task.tss);
        p++;
        set_ldt_desc(p, &default_ldt, 1);
        p++;
        for(i=1 ; i<NR_TASKS ; i++) {
                p->a=p->b=0;
                p++;
                p->a=p->b=0;
                p++;
        }
/* Clear NT, so that we won't have troubles with that later on */
#if (!SIX)
        __asm__("pushfl ; andl $0xffffbfff,(%esp) ; popfl");
#endif
        load_TR(0);
        load_ldt(0);
}


