
#ifndef _LINUX_BINFMTS_H
#define _LINUX_BINFMTS_H

#include <linux/ptrace.h>

/*
 * MAX_ARG_PAGES defines the number of pages allocated for arguments
 * and envelope for the new program. 32 should suffice, this gives
 * a maximum env+arg of 128kB w/4KB pages!
 */
#define MAX_ARG_PAGES 32

/*
 * This structure is used to hold the arguments that are used when loading binar
ies.
 */
struct linux_binprm{
        char buf[128];
        unsigned long page[MAX_ARG_PAGES];
        unsigned long p;
        int sh_bang;
        struct inode * inode;
        int e_uid, e_gid;
        int argc, envc;
        char * filename;        /* Name of binary */
        unsigned long loader, exec;
        int dont_iput;          /* binfmt handler has put inode */
};

/*
 * This structure defines the functions that are used to load the binary formats
 that
 * linux accepts.
 */
struct linux_binfmt {
        struct linux_binfmt * next;
        long *use_count;
        int (*load_binary)(struct linux_binprm *, struct  pt_regs * regs);
        int (*load_shlib)(int fd);
        int (*core_dump)(long signr, struct pt_regs * regs);
};




#endif

