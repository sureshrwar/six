
#ifndef _ASM_SIX_PAGE_H
#define _ASM_SIX_PAGE_H

#define PROT_READ       0x1             /* pages can be read */
#define PROT_WRITE      0x2             /* pages can be written */
#define PROT_EXEC       0x4             /* pages can be executed */
#define MAP_SHARED      1               /* share changes */
#define MAP_FIXED       0x10            /* user assigns address */
#define MAP_FAILED      ((void *) -1)   /* mmap failure value */

#if (__i386__)
#define CLICK_SIZE 4096
#else
#define CLICK_SIZE 8192
#endif

#define A_MAGIC0      (unsigned char) 0x03    /* SPARC has opposite byte */
#define A_MAGIC1      (unsigned char) 0x03
#define A_SUNOS 0x23
#define A_SEP   0x20
#define A_EXEC  0x10 

#define TEXT 0
#define DATA 1
#define RODATA 2
#define BSS 3
#define NUM_AREAS (BSS + 1)

#define downclick(addr) ((unsigned)(addr) & ~(CLICK_SIZE-1))
#define upclick(addr) downclick((unsigned)(addr) + CLICK_SIZE-1)


#ifdef STRICT_MM_TYPECHECKS

typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pmd; } pmd_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;

#define pte_val(x)      ((x).pte)
#define pmd_val(x)      ((x).pmd)
#define pgd_val(x)      ((x).pgd)
#define pgprot_val(x)   ((x).pgprot)

#define __pte(x)        ((pte_t) { (x) } )
#define __pgd(x)        ((pgd_t) { (x) } )
#define __pgprot(x)     ((pgprot_t) { (x) } )

#else

typedef unsigned long pte_t;
typedef unsigned long pmd_t;
typedef unsigned long pgd_t;
typedef unsigned long pgprot_t;

#define pte_val(x)      (x)
#define pmd_val(x)      (x)
#define pgd_val(x)      (x)
#define pgprot_val(x)   (x)

#define __pte(x)        (x)
#define __pgprot(x)     (x)
#define __pmd(x)        ((pmd_t) { (x) } )

#endif

/* PAGE_SHIFT determines the page size */
#if (__i386__)
#define PAGE_SHIFT      12	/* 4096 */
#else
#define PAGE_SHIFT      13	/* 8192 */
#endif
#define PAGE_SIZE       (1UL << PAGE_SHIFT)
#define PAGE_MASK       (~(PAGE_SIZE-1))

/* This handles the memory map.. */
#define PAGE_OFFSET             0
#define MAP_NR(addr)            (((unsigned long)(addr)) >> PAGE_SHIFT)

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)        (((addr)+PAGE_SIZE-1)&PAGE_MASK)

/*must be aligned on an 8 byte boundary*/
#define stack_align(sp) ((sp) & ~7)

#endif
