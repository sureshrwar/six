
/*
 * include/asm-i386/processor.h
 *
 * Copyright (C) 1994 Linus Torvalds
 */

#ifndef __ASM_I386_PROCESSOR_H
#define __ASM_I386_PROCESSOR_H

#include <asm/vm86.h>
#include <asm/math_emu.h>
#include <solaris.h>

extern char hard_math;
extern int  x86_capability;     /* field of flags */
extern char ignore_irq13;
extern char wp_works_ok;        /* doesn't work on a 386 */

/*
 * Bus types (default is ISA, but people can check others with these..)
 * MCA_bus hardcoded to 0 for now.
 */
extern int EISA_bus;

/*
 * no task is gonna go beyond this.
 */
#define TASK_SIZE       STACK_BASE

#define IO_BITMAP_SIZE  32

struct i387_hard_struct {
        long    cwd;
        long    swd;
        long    twd;
        long    fip;
        long    fcs;
        long    foo;
        long    fos;
        long    st_space[20];   /* 8*10 bytes for each FP-reg = 80 bytes */
        long    status;         /* software status information */
};

struct i387_soft_struct {
        long    cwd;
        long    swd;
        long    twd;
        long    fip;
        long    fcs;
        long    foo;
        long    fos;
        long    top;
        struct fpu_reg  regs[8];        /* 8*16 bytes for each FP-reg = 128 byte
s */
        unsigned char   lookahead;
        struct info     *info;
        unsigned long   entry_eip;
};


union i387_union {
        struct i387_hard_struct hard;
        struct i387_soft_struct soft;
};

struct thread_struct {
        unsigned short  back_link,__blh;
        unsigned long   esp0;
        unsigned short  ss0,__ss0h;
        unsigned long   esp1;
        unsigned short  ss1,__ss1h;
        unsigned long   esp2;
        unsigned short  ss2,__ss2h;
        unsigned long   cr3;
        unsigned long   eip;
        unsigned long   eflags;
        unsigned long   eax,ecx,edx,ebx;
        unsigned long   esp;
        unsigned long   ebp;
        unsigned long   esi;
        unsigned long   edi;
        unsigned short  es, __esh;
        unsigned short  cs, __csh;
        unsigned short  ss, __ssh;
        unsigned short  ds, __dsh;
        unsigned short  fs, __fsh;
        unsigned short  gs, __gsh;
        unsigned short  ldt, __ldth;
        unsigned short  trace, bitmap;
        unsigned long   io_bitmap[IO_BITMAP_SIZE+1];
        unsigned long   tr;
        unsigned long    trap_no, error_code;
};

/*
 * this is the vm_area_struct of init_task's mm_struct. note that vm_start and
 * vm_end are zeroes. so when init_task calls for a fork(), dup_mmap() doesnt
 * have much work to do. advantages of a user-mode kernel :)
 * dup_mmap() has to work hard only when a mapped process - that is, a process
 * whose text/data has been loaded from the disk instead of being part of the
 * kernel - calls a fork.
 */
#define INIT_MMAP { &init_mm, 0, 0, PAGE_SHARED, VM_READ | VM_WRITE | VM_EXEC }

#define INIT_TSS  { \
        0,0, \
}

static inline start_thread(struct pt_regs * regs, unsigned long eip, unsigned long esp)
{
}              

#if (SIX)
/*
 * dunno for sure, but me thinks we need more stack.
 */
#define alloc_kernel_stack()    __get_free_pages(GFP_KERNEL, 1, 0)
#define free_kernel_stack(page) free_pages((page), 1)
#else
#define alloc_kernel_stack()    get_free_page(GFP_KERNEL)
#define free_kernel_stack(page) free_page((page))
#endif

/*
 * Return saved PC of a blocked thread.  This assumes the frame
 * pointer is the 6th saved long on the kernel stack and that the
 * saved return address is the first long in the frame.  This all
 * holds provided the thread blocked through a call to schedule() ($15
 * is the frame pointer in schedule() and $15 is saved at offset 48 by
 * entry.S:do_switch_stack).
 */     
static inline unsigned long thread_saved_pc(struct thread_struct *t)
{        
        unsigned long fp;
         
#if 0
        fp = ((unsigned long*)t->ksp)[6];
#endif
        return *(unsigned long*)fp;
}


#endif
