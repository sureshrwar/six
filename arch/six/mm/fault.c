
/*              
 *  linux/arch/alpha/mm/fault.c 
 * 
 *  Copyright (C) 1995  Linus Torvalds
 */

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h> 
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
 
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/pgtable.h>
#include <asm/mmu_context.h>



#ifndef BROKEN_ASN
/*
 * Select a new ASN and reload the context. This is
 * not inlined as this expands to a pretty large
 * function.
 */
void get_new_asn_and_reload(struct task_struct *tsk, struct mm_struct *mm)
{

}
#endif

