
#ifndef _ASM_SIX_PGTABLE_H
#define _ASM_SIX_PGTABLE_H

#include <linux/config.h>

/*
 * The Linux memory management assumes a three-level page table setup. On
 * the i386, we use that, but "fold" the mid level into the top-level page
 * table, so that we physically have the same two-level page table as the
 * i386 mmu expects.
 *
 * This file contains the functions and defines necessary to modify and use
 * the i386 page table tree.
 */

#define flush_cache_all()                       do { } while (0)
#define flush_cache_mm(mm)                      do { } while (0)
#define flush_cache_range(mm, start, end)       do { } while (0)
#define flush_cache_page(vma, vmaddr)           do { } while (0)
#define flush_page_to_ram(page)                 do { } while (0)

#define __flush_tlb()				do { } while (0)
#define tbi(a, b)				do { } while (0)

#define flush_tlb() __flush_tlb()
#define flush_tlb_all() __flush_tlb()

/*
 * Use a few helper functions to hide the ugly broken ASN
 * numbers on early alpha's (ev4 and ev45)
 */
#ifdef BROKEN_ASN

#define flush_tlb_other(x) do { } while (0)
#define flush_tlb_current(mm) get_new_asn_and_reload(current, mm)

#else

#define flush_tlb_current(mm) get_new_asn_and_reload(current, mm)
#define flush_tlb_other(mm) do { (mm)->context = 0; } while (0)

#endif

/*
 * Flush just one page in the current TLB set.
 * We need to be very careful about the icache here, there
 * is no way to invalidate a specific icache page..
 */
static inline void flush_tlb_current_page(struct mm_struct * mm,
        struct vm_area_struct *vma,
        unsigned long addr)
{
#ifdef BROKEN_ASN
        tbi(2 + ((vma->vm_flags & VM_EXEC) != 0), addr);
#else
        if (vma->vm_flags & VM_EXEC)
                flush_tlb_current(mm);
        else
                tbi(2, addr);
#endif
}

extern void get_new_asn_and_reload(struct task_struct *, struct mm_struct *);

static inline void flush_tlb_mm(struct mm_struct *mm)
{
        if (mm == current->mm)
                __flush_tlb();
}

static inline void flush_tlb_range(struct mm_struct *mm,
        unsigned long start, unsigned long end)
{
        if (mm == current->mm)
                __flush_tlb();
}

/*      
 * Page-granular tlb flush. 
 *      
 * do a tbisd (type = 2) normally, and a tbis (type = 3)
 * if it is an executable mapping.  We want to avoid the
 * itlb flush, because that potentially also does a
 * icache flush.
 */     
static inline void flush_tlb_page(struct vm_area_struct *vma,
        unsigned long addr)
{       
        struct mm_struct * mm = vma->vm_mm;
        
        if (mm != current->mm)
                flush_tlb_other(mm);
        else
                flush_tlb_current_page(mm, vma, addr);
}       




/* Certain architectures need to do special things when pte's
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
#define set_pte(pteptr, pteval) ((*(pteptr)) = (pteval))

/* PMD_SHIFT determines the size of the area a second-level page table can map */
#if (__i386__)
#define PMD_SHIFT       22
#else
#define PMD_SHIFT       24
#endif
#define PMD_SIZE        (1UL << PMD_SHIFT)
#define PMD_MASK        (~(PMD_SIZE-1))

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#if (__i386__)
#define PGDIR_SHIFT     22
#else
#define PGDIR_SHIFT     24
#endif
#define PGDIR_SIZE      (1UL << PGDIR_SHIFT)
#define PGDIR_MASK      (~(PGDIR_SIZE-1))

#if (__i386__)
#define PTRS_PER_PTE    1024*1 /* our pagesize is 8192 - which can hold 2048 addresses */
#define PTRS_PER_PMD    1
#define PTRS_PER_PGD    1024*1 /* yeah same applies here,too */
#else /* sparc */
#define PTRS_PER_PTE    1024*2 /* our pagesize is 8192 - which can hold 2048 addresses */
#define PTRS_PER_PMD    1
#define PTRS_PER_PGD    1024*2 /* yes same applies here,too */
#endif

/* Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 8MB value just means that there will be a 8MB "hole" after the
 * physical memory until the kernel virtual memory starts.  That means that
 * any out-of-bounds memory accesses will hopefully be caught.
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;)
 */
#define VMALLOC_OFFSET  (8*1024*1024)
#if (SIX)
#define VMALLOC_START TASK_SIZE
#define VMALLOC_VMADDR(x) (unsigned long)(x)
#else
#define VMALLOC_START ((high_memory + VMALLOC_OFFSET) & ~(VMALLOC_OFFSET-1))
#define VMALLOC_VMADDR(x) (TASK_SIZE + (unsigned long)(x))
#endif

/*
 * The 4MB page is guessing..  Detailed in the infamous "Chapter H"
 * of the Pentium details, but assuming intel did the straightforward
 * thing, this bit set in the page directory entry just means that
 * the page directory entry points directly to a 4MB-aligned block of
 * memory.
 */
#define _PAGE_PRESENT   0x001
#define _PAGE_RW        0x002
#define _PAGE_USER      0x004
#define _PAGE_PCD       0x010
#define _PAGE_ACCESSED  0x020
#define _PAGE_DIRTY     0x040
#define _PAGE_4M        0x080   /* 4 MB page, Pentium+.. */

/*
 * OSF/1 PAL-code-imposed page table bits
 */
#define _PAGE_VALID     0x0001
#define _PAGE_FOR       0x0002  /* used for page protection (fault on read) */
#define _PAGE_FOW       0x0004  /* used for page protection (fault on write) */
#define _PAGE_FOE       0x0008  /* used for page protection (fault on exec) */
#define _PAGE_ASM       0x0010
#define _PAGE_KRE       0x0100  /* xxx - see below on the "accessed" bit */
#define _PAGE_URE       0x0200  /* xxx */
#define _PAGE_KWE       0x1000  /* used to do the dirty bit in software */
#define _PAGE_UWE       0x2000  /* used to do the dirty bit in software */


#define _PAGE_TABLE     (_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY)
#define _PAGE_CHG_MASK  (PAGE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY)

#define PAGE_NONE       __pgprot(_PAGE_PRESENT | _PAGE_ACCESSED)
#define PAGE_SHARED     __pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_COPY       __pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_READONLY   __pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_KERNEL     __pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY | _PAGE_ACCESSED)


/*
 * The i386 can't do page protection for execute, and considers that the same are read.
 * Also, write permissions imply read permissions. This is the closest we can get..
 */
#define __P000  PAGE_NONE
#define __P001  PAGE_READONLY
#define __P010  PAGE_COPY
#define __P011  PAGE_COPY
#define __P100  PAGE_READONLY
#define __P101  PAGE_READONLY
#define __P110  PAGE_COPY
#define __P111  PAGE_COPY

#define __S000  PAGE_NONE
#define __S001  PAGE_READONLY
#define __S010  PAGE_SHARED
#define __S011  PAGE_SHARED
#define __S100  PAGE_READONLY
#define __S101  PAGE_READONLY
#define __S110  PAGE_SHARED
#define __S111  PAGE_SHARED


/*
 * BAD_PAGETABLE is used when we need a bogus page-table, while
 * BAD_PAGE is used for a bogus page.
 *
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
#define __bad_page()	1
#define __bad_pagetable()	1

#define BAD_PAGETABLE __bad_pagetable()
#define BAD_PAGE __bad_page()


/* page table for 0-4MB for everybody */
extern unsigned long pg0[1024];
/* zero page used for uninitialized stuff */
extern char empty_zero_page[PAGE_SIZE];

#define ZERO_PAGE ((unsigned long) empty_zero_page)

#define pte_clear(xp)   do { pte_val(*(xp)) = 0; } while (0)
#define pmd_none(x)     (!pmd_val(x))
#define pmd_bad(x)      ((pmd_val(x) & ~PAGE_MASK) != _PAGE_TABLE)
#define pmd_present(x)  (pmd_val(x) & _PAGE_PRESENT)
#define pmd_clear(xp)   do { pmd_val(*(xp)) = 0; } while (0)

static inline int pte_write(pte_t pte)          { return !(pte_val(pte) & _PAGE_FOW); }

static inline pte_t mk_pte(unsigned long page, pgprot_t pgprot)
{
        pte_t pte;
        pte_val(pte) = page | pgprot_val(pgprot);
        return pte;
}

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	pte_val(pte) = (pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot);
	return pte;
}

static inline unsigned long pte_page(pte_t pte)
{ return pte_val(pte) & PAGE_MASK; }

static inline void dup_pte(pte_t *dst, pte_t *src)
{
	unsigned long page;
	page = get_free_page(GFP_KERNEL);	
	memcpy((void *)page, (void *)pte_page(*src), PAGE_SIZE); 
	*dst = page | _PAGE_TABLE;
}

static inline unsigned long pmd_page(pmd_t pmd)
{ return pmd_val(pmd) & PAGE_MASK; }


static inline pgd_t * pgd_offset(struct mm_struct * mm, unsigned long address)
{
        return mm->pgd + (address >> PGDIR_SHIFT);
}

static inline pte_t * pte_offset(pmd_t * dir, unsigned long address)
{
        return (pte_t *) pmd_page(*dir) + ((address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1));
}

/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any.
 */

static inline void pte_free(pte_t * pte)
{
        free_page((unsigned long) pte);
}

static inline void pgd_free(pgd_t * pgd)
{
        free_page((unsigned long) pgd);
}

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
static inline void pmd_free(pmd_t * pmd)
{
        pmd_val(*pmd) = 0;
}

static inline pte_t * pte_alloc_kernel(pmd_t * pmd, unsigned long address)
{
        address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
        if (pmd_none(*pmd)) {
                pte_t * page = (pte_t *) get_free_page(GFP_KERNEL);
                if (pmd_none(*pmd)) {
                        if (page) {
                                pmd_val(*pmd) = _PAGE_TABLE | (unsigned long) page;
                                return page + address;
                        }
                        pmd_val(*pmd) = _PAGE_TABLE | (unsigned long) BAD_PAGETABLE;
                        return NULL;
                }
                free_page((unsigned long) page);
        }
        if (pmd_bad(*pmd)) {
                printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
                pmd_val(*pmd) = _PAGE_TABLE | (unsigned long) BAD_PAGETABLE;
                return NULL;
        }
        return (pte_t *) pmd_page(*pmd) + address;
}

/* Find an entry in the second-level page table.. */
static inline pmd_t * pmd_offset(pgd_t * dir, unsigned long address)
{
        return (pmd_t *) dir;
}

static inline pgd_t * pgd_alloc(void)
{
        return (pgd_t *) get_free_page(GFP_KERNEL);
}

static inline pmd_t * pmd_alloc_kernel(pgd_t * pgd, unsigned long address)
{
        return (pmd_t *) pgd;
}


static inline pmd_t * pmd_alloc(pgd_t * pgd, unsigned long address)
{
        return (pmd_t *) pgd;
}

static inline pte_t * pte_alloc(pmd_t * pmd, unsigned long address)
{
        address = (address >> (PAGE_SHIFT-2)) & 4*(PTRS_PER_PTE - 1);

repeat:
        if (pmd_none(*pmd))
                goto getnew;
        if (pmd_bad(*pmd))
                goto fix;
        return (pte_t *) (pmd_page(*pmd) + address);

getnew:
{
        unsigned long page = __get_free_page(GFP_KERNEL);
        if (!pmd_none(*pmd))
                goto freenew;
        if (!page)
                goto oom;
        memset((void *) page, 0, PAGE_SIZE);
        pmd_val(*pmd) = _PAGE_TABLE | page;
        return (pte_t *) (page + address);
freenew:
        free_page(page);
        goto repeat;
}

fix:
        printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
oom:
        pmd_val(*pmd) = _PAGE_TABLE | (unsigned long) BAD_PAGETABLE;
        return NULL;
}



extern pgd_t swapper_pg_dir[1024];

#define SWP_TYPE(entry) (((entry) >> 1) & 0x7f)
#define SWP_OFFSET(entry) ((entry) >> 8)
#define SWP_ENTRY(type,offset) (((type) << 1) | ((offset) << 8))

/*
 * TLB flushing:
 *
 *  - flush_tlb() flushes the current mm struct TLBs
 *  - flush_tlb_all() flushes all processes TLBs
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(mm, start, end) flushes a range of pages
 *
 * ..but the i386 has somewhat limited tlb flushing capabilities,
 * and page-granular flushes are available only on i486 and up.
 */


#define __flush_tlb()			do { } while (0)
#define flush_tlb() __flush_tlb()

/* to set the page-dir */
#define SET_PAGE_DIR(tsk,pgdir) \
	(tsk)->tss.cr3 = (unsigned long) (pgdir); \

#define pte_none(x)     (!pte_val(x))
#define pte_present(x)  (pte_val(x) & _PAGE_PRESENT)
#define pte_clear(xp)   do { pte_val(*(xp)) = 0; } while (0)

static inline int pte_read(pte_t pte)           { return pte_val(pte) & _PAGE_USER; }
static inline int pte_exec(pte_t pte)           { return pte_val(pte) & _PAGE_USER; }
static inline int pte_dirty(pte_t pte)          { return pte_val(pte) & _PAGE_DIRTY; }
static inline int pte_young(pte_t pte)          { return pte_val(pte) & _PAGE_ACCESSED; }

static inline pte_t pte_wrprotect(pte_t pte)    { pte_val(pte) &= ~_PAGE_RW; return pte; }
static inline pte_t pte_rdprotect(pte_t pte)    { pte_val(pte) &= ~_PAGE_USER; return pte; }
static inline pte_t pte_exprotect(pte_t pte)    { pte_val(pte) &= ~_PAGE_USER; return pte; }
static inline pte_t pte_mkclean(pte_t pte)      { pte_val(pte) &= ~_PAGE_DIRTY; return pte; }
static inline pte_t pte_mkold(pte_t pte)        { pte_val(pte) &= ~_PAGE_ACCESSED; return pte; }
static inline pte_t pte_mkwrite(pte_t pte)      { pte_val(pte) |= _PAGE_RW; return pte; }
static inline pte_t pte_mkread(pte_t pte)       { pte_val(pte) |= _PAGE_USER; return pte; }
static inline pte_t pte_mkexec(pte_t pte)       { pte_val(pte) |= _PAGE_USER; return pte; }
static inline pte_t pte_mkdirty(pte_t pte)      { pte_val(pte) |= _PAGE_DIRTY; return pte; }
static inline pte_t pte_mkyoung(pte_t pte)      { pte_val(pte) |= _PAGE_ACCESSED; return pte; }




/*
 * The "pgd_xxx()" functions here are trivial for a folded two-level
 * setup: the pgd is never bad, and a pmd always exists (as it's folded
 * into the pgd entry)
 */
static inline int pgd_none(pgd_t pgd)           { return 0; }
static inline int pgd_bad(pgd_t pgd)            { return 0; }
static inline int pgd_present(pgd_t pgd)        { return 1; }
static inline void pgd_clear(pgd_t * pgdp)      { }




#endif


