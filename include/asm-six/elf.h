
#ifndef __ASMi386_ELF_H
#define __ASMi386_ELF_H 

/*      
 * ELF register definitions..
 */     
         
#include <asm/ptrace.h>
         
typedef unsigned long elf_greg_t;
         
#define ELF_NGREG (sizeof (struct pt_regs) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

/*        
 * This is used to ensure we don't load something for the wrong architecture.
 */               
#define elf_check_arch(x) ( ((x) == EM_386) || ((x) == EM_486) )

/*      
 * These are used to set parameters in the core dumps.
 */
#define USE_ELF_CORE_DUMP 
#define ELF_EXEC_PAGESIZE       4096

#endif  

