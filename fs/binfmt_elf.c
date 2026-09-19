
/*
 * linux/fs/binfmt_elf.c
 *
 * These are the functions used to load ELF format executables as used
 * on SVr4 machines.  Information on the format may be found in the book
 * "UNIX SYSTEM V RELEASE 4 Programmers Guide: Ansi C and Programming Support
 * Tools".
 *
 * Copyright 1993, 1994: Eric Youngdale (ericy@cais.com).
 */

#include <linux/module.h>

#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/a.out.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/binfmts.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/shm.h>
#include <linux/personality.h>
#include <linux/elfcore.h>

#include <asm/segment.h>
#include <asm/pgtable.h>

#include <linux/config.h>

#define DLINFO_ITEMS 12

#include <linux/elf.h>

/* Forward declarations hoisted for modern GCC -- these statics are
 * called earlier in this file than they are defined.  Older gcc
 * accepted the resulting implicit declaration; modern gcc does not.
 */
static load_six_elf_binary(struct linux_binprm * bprm, struct pt_regs *regs);


static int load_elf_binary(struct linux_binprm * bprm, struct pt_regs * regs);
static int load_elf_library(int fd);

/*
 * If we don't support core dumping, then supply a NULL so we
 * don't even try.
 */
#ifdef USE_ELF_CORE_DUMP
static int elf_core_dump(long signr, struct pt_regs * regs);
#else
#define elf_core_dump   NULL
#endif

#define ELF_PAGESTART(_v) ((_v) & ~(unsigned long)(ELF_EXEC_PAGESIZE-1))
#define ELF_PAGEOFFSET(_v) ((_v) & (ELF_EXEC_PAGESIZE-1))


static struct linux_binfmt elf_format = {
#ifndef MODULE
        NULL, NULL, load_elf_binary, load_elf_library,  elf_core_dump
#else
        NULL, &mod_use_count_, load_elf_binary, load_elf_library, elf_core_dump
#endif
};


static void set_brk(unsigned long start, unsigned long end)
{
        start = PAGE_ALIGN(start);
        end = PAGE_ALIGN(end);
        if (end <= start)
                return;
        do_mmap(NULL, start, end - start,
                PROT_READ | PROT_WRITE | PROT_EXEC,
                MAP_FIXED | MAP_PRIVATE, 0);
}

/* We need to explicitly zero any fractional pages
   after the data section (i.e. bss).  This would
   contain the junk from the file that should not
   be in memory */


static void padzero(unsigned long elf_bss)
{
        unsigned long nbyte;
        char * fpnt;

        nbyte = elf_bss & (PAGE_SIZE-1);
        if (nbyte) {
                nbyte = PAGE_SIZE - nbyte;
                /* FIXME: someone should investigate, why a bad binary
                   is allowed to bring a wrong elf_bss until here,
                   and how to react. Suffice the plain return?
                   rossius@hrz.tu-chemnitz.de */
                if (verify_area(VERIFY_WRITE, (void *) elf_bss, nbyte)) {
                        return;
                }
                fpnt = (char *) elf_bss;
                do {
                        put_user(0, fpnt++);
                } while (--nbyte);
        }
}

unsigned long * create_elf_tables(char *p, int argc, int envc,
                                  struct elfhdr * exec,
                                  unsigned long load_addr,
                                  unsigned long interp_load_addr, int ibcs)
{
        unsigned long *argv, *envp, *dlinfo;
        unsigned long *sp;

        /*
         * Force 16 byte alignment here for generality.
         */
        sp = (unsigned long *) (~15UL & (unsigned long) p);
        sp -= exec ? DLINFO_ITEMS*2 : 2;
        dlinfo = sp;
        sp -= envc+1;
        envp = sp;
        sp -= argc+1;
        argv = sp;
        if (!ibcs) {
                put_user(envp,--sp);
                put_user(argv,--sp);
        }

#define NEW_AUX_ENT(id, val) \
          put_user ((id), dlinfo++); \
          put_user ((val), dlinfo++)

        if (exec) { /* Put this here for an ELF program interpreter */
          struct elf_phdr * eppnt;
          eppnt = (struct elf_phdr *) exec->e_phoff;

          NEW_AUX_ENT (AT_PHDR, load_addr + exec->e_phoff);
          NEW_AUX_ENT (AT_PHENT, sizeof (struct elf_phdr));
          NEW_AUX_ENT (AT_PHNUM, exec->e_phnum);
          NEW_AUX_ENT (AT_PAGESZ, PAGE_SIZE);
          NEW_AUX_ENT (AT_BASE, interp_load_addr);
          NEW_AUX_ENT (AT_FLAGS, 0);
          NEW_AUX_ENT (AT_ENTRY, (unsigned long) exec->e_entry);
          NEW_AUX_ENT (AT_UID, (unsigned long) current->uid);
          NEW_AUX_ENT (AT_EUID, (unsigned long) current->euid);
          NEW_AUX_ENT (AT_GID, (unsigned long) current->gid);
          NEW_AUX_ENT (AT_EGID, (unsigned long) current->egid);
        }
        NEW_AUX_ENT (AT_NULL, 0);
#undef NEW_AUX_ENT
        put_user((unsigned long)argc,--sp);
        current->mm->arg_start = (unsigned long) p;
        while (argc-->0) {
                put_user(p,argv++);
                while (get_user(p++)) /* nothing */ ;
        }
        put_user(0,argv);
        current->mm->arg_end = current->mm->env_start = (unsigned long) p;
        while (envc-->0) {
                put_user(p,envp++);
                while (get_user(p++)) /* nothing */ ;
        }
        put_user(0,envp);
        current->mm->env_end = (unsigned long) p;
        return sp;
}



/* This is much more generalized than the library routine read function,
   so we keep this separate.  Technically the library read function
   is only provided so that we can read a.out libraries that have
   an ELF header */

static unsigned long load_elf_interp(struct elfhdr * interp_elf_ex,
                                     struct inode * interpreter_inode,
                                     unsigned long *interp_load_addr)
{
        struct file * file;
        struct elf_phdr *elf_phdata  =  NULL;
        struct elf_phdr *eppnt;
        unsigned long load_addr;
        int elf_exec_fileno;
        int retval;
        unsigned long last_bss, elf_bss;
        unsigned long error;
        int i;

        elf_bss = 0;
        last_bss = 0;
        error = load_addr = 0;

        /* First of all, some simple consistency checks */
        if ((interp_elf_ex->e_type != ET_EXEC &&
            interp_elf_ex->e_type != ET_DYN) ||
           !elf_check_arch(interp_elf_ex->e_machine) ||
           (!interpreter_inode->i_op ||
            !interpreter_inode->i_op->default_file_ops->mmap)){
                return ~0UL;
        }

        /* Now read in all of the header information */

        if (sizeof(struct elf_phdr) * interp_elf_ex->e_phnum > PAGE_SIZE)
            return ~0UL;

        elf_phdata =  (struct elf_phdr *)
                kmalloc(sizeof(struct elf_phdr) * interp_elf_ex->e_phnum,
                        GFP_KERNEL);
        if (!elf_phdata)
          return ~0UL;

        /*
         * If the size of this structure has changed, then punt, since
         * we will be doing the wrong thing.
         */
        if (interp_elf_ex->e_phentsize != sizeof(struct elf_phdr))
          {
            kfree(elf_phdata);
            return ~0UL;
          }


        retval = read_exec(interpreter_inode, interp_elf_ex->e_phoff,
                           (char *) elf_phdata,
                           sizeof(struct elf_phdr) * interp_elf_ex->e_phnum, 1);

        if (retval < 0) {
                kfree (elf_phdata);
                return retval;
        }

        elf_exec_fileno = open_inode(interpreter_inode, O_RDONLY);
        if (elf_exec_fileno < 0) {
          kfree(elf_phdata);
          return ~0UL;
        }

        file = current->files->fd[elf_exec_fileno];

        eppnt = elf_phdata;
        for(i=0; i<interp_elf_ex->e_phnum; i++, eppnt++)
          if (eppnt->p_type == PT_LOAD) {
            int elf_type = MAP_PRIVATE | MAP_DENYWRITE;
            int elf_prot = 0;
            unsigned long vaddr = 0;
            unsigned long k;

            if (eppnt->p_flags & PF_R) elf_prot =  PROT_READ;
            if (eppnt->p_flags & PF_W) elf_prot |= PROT_WRITE;
            if (eppnt->p_flags & PF_X) elf_prot |= PROT_EXEC;
            if (interp_elf_ex->e_type == ET_EXEC || load_addr != 0) {
                elf_type |= MAP_FIXED;
                vaddr = eppnt->p_vaddr;
            }

            error = do_mmap(file,
                            load_addr + ELF_PAGESTART(vaddr),
                            eppnt->p_filesz + ELF_PAGEOFFSET(eppnt->p_vaddr),
                            elf_prot,
                            elf_type,
                            ELF_PAGESTART(eppnt->p_offset));

            if (error > -1024UL) {
              /* Real error */
              sys_close(elf_exec_fileno);
              kfree(elf_phdata);
              return ~0UL;
            }

            if (!load_addr && interp_elf_ex->e_type == ET_DYN)
              load_addr = error;

            /*
             * Find the end of the file  mapping for this phdr, and keep
             * track of the largest address we see for this.
             */
            k = load_addr + eppnt->p_vaddr + eppnt->p_filesz;
            if (k > elf_bss) elf_bss = k;

            /*
             * Do the same thing for the memory mapping - between
             * elf_bss and last_bss is the bss section.
             */
            k = load_addr + eppnt->p_memsz + eppnt->p_vaddr;
            if (k > last_bss) last_bss = k;
          }

        /* Now use mmap to map the library into memory. */

        sys_close(elf_exec_fileno);

        /*
         * Now fill out the bss section.  First pad the last page up
         * to the page boundary, and then perform a mmap to make sure
         * that there are zeromapped pages up to and including the last
         * bss page.
         */
        padzero(elf_bss);
        elf_bss = ELF_PAGESTART(elf_bss + ELF_EXEC_PAGESIZE - 1); /* What we have mapped so far */

        /* Map the last of the bss segment */
        if (last_bss > elf_bss)
          do_mmap(NULL, elf_bss, last_bss-elf_bss,
                  PROT_READ|PROT_WRITE|PROT_EXEC,
                  MAP_FIXED|MAP_PRIVATE, 0);
        kfree(elf_phdata);

        *interp_load_addr = load_addr;
        return ((unsigned long) interp_elf_ex->e_entry) + load_addr;
}

static unsigned long load_aout_interp(struct exec * interp_ex,
                             struct inode * interpreter_inode)
{
  int retval;
  unsigned long elf_entry;

  current->mm->brk = interp_ex->a_bss +
    (current->mm->end_data = interp_ex->a_data +
     (current->mm->end_code = interp_ex->a_text));
  elf_entry = interp_ex->a_entry;


  if (N_MAGIC(*interp_ex) == OMAGIC) {
    do_mmap(NULL, 0, interp_ex->a_text+interp_ex->a_data,
            PROT_READ|PROT_WRITE|PROT_EXEC,
            MAP_FIXED|MAP_PRIVATE, 0);
    retval = read_exec(interpreter_inode, 32, (char *) 0,
                       interp_ex->a_text+interp_ex->a_data, 0);
  } else if (N_MAGIC(*interp_ex) == ZMAGIC || N_MAGIC(*interp_ex) == QMAGIC) {
    do_mmap(NULL, 0, interp_ex->a_text+interp_ex->a_data,
            PROT_READ|PROT_WRITE|PROT_EXEC,
            MAP_FIXED|MAP_PRIVATE, 0);
    retval = read_exec(interpreter_inode,
                       N_TXTOFF(*interp_ex) ,
                       (char *) N_TXTADDR(*interp_ex),
                       interp_ex->a_text+interp_ex->a_data, 0);
  } else
    retval = -1;

  if (retval >= 0)
    do_mmap(NULL, ELF_PAGESTART(interp_ex->a_text + interp_ex->a_data + ELF_EXEC_PAGESIZE - 1),
            interp_ex->a_bss,
            PROT_READ|PROT_WRITE|PROT_EXEC,
            MAP_FIXED|MAP_PRIVATE, 0);
  if (retval < 0) return ~0UL;
  return elf_entry;
}



/*
 * These are the functions used to load ELF style executables and shared
 * libraries.  There is no binary dependent code anywhere else.
 */

#define INTERPRETER_NONE 0
#define INTERPRETER_AOUT 1
#define INTERPRETER_ELF 2


static inline int
do_load_elf_binary(struct linux_binprm * bprm, struct pt_regs * regs)
{
        struct elfhdr elf_ex;
        struct elfhdr interp_elf_ex;
        struct file * file;
        struct exec interp_ex;
        struct inode *interpreter_inode;
        unsigned long load_addr;
        unsigned int interpreter_type = INTERPRETER_NONE;
        unsigned char ibcs2_interpreter;
        int i;
        int old_fs;
        int error;
        struct elf_phdr * elf_ppnt, *elf_phdata;
        int elf_exec_fileno;
        unsigned long elf_bss, k, elf_brk;
        int retval;
        char * elf_interpreter;
        unsigned long elf_entry, interp_load_addr = 0;
        int status;
        unsigned long start_code, end_code, end_data;
        unsigned long elf_stack;
        char passed_fileno[6];

        ibcs2_interpreter = 0;
        status = 0;
        load_addr = 0;
        elf_ex = *((struct elfhdr *) bprm->buf);          /* exec-header */

        if (elf_ex.e_ident[0] != 0x7f ||
            strncmp(&elf_ex.e_ident[1], "ELF",3) != 0) {
                return  -ENOEXEC;
        }


        /* First of all, some simple consistency checks */
		/*
        if ((elf_ex.e_type != ET_EXEC &&
            elf_ex.e_type != ET_DYN) ||
           (! elf_check_arch(elf_ex.e_machine)) ||
           (!bprm->inode->i_op || !bprm->inode->i_op->default_file_ops ||
            !bprm->inode->i_op->default_file_ops->mmap)){
                return -ENOEXEC;
        }
		*/

        /* Now read in all of the header information */

        elf_phdata = (struct elf_phdr *) kmalloc(elf_ex.e_phentsize *
                                                 elf_ex.e_phnum, GFP_KERNEL);
        if (elf_phdata == NULL) {
                return -ENOMEM;
        }
        retval = read_exec(bprm->inode, elf_ex.e_phoff, (char *) elf_phdata,
                           elf_ex.e_phentsize * elf_ex.e_phnum, 1);
        if (retval < 0) {
                kfree (elf_phdata);
                return retval;
        }

        elf_ppnt = elf_phdata;

        elf_bss = 0;
        elf_brk = 0;

        elf_exec_fileno = open_inode(bprm->inode, O_RDONLY);

        if (elf_exec_fileno < 0) {
                kfree (elf_phdata);
                return elf_exec_fileno;
        }

        file = current->files->fd[elf_exec_fileno];

        elf_stack = ~0UL;
        elf_interpreter = NULL;
        start_code = ~0UL;
        end_code = 0;
        end_data = 0;

        for(i=0;i < elf_ex.e_phnum; i++){
                if (elf_ppnt->p_type == PT_INTERP) {
                        if ( elf_interpreter != NULL )
                        {
                                kfree (elf_phdata);
                                kfree(elf_interpreter);
                                sys_close(elf_exec_fileno);
                                return -EINVAL;
                        }

                        /* This is the program interpreter used for
                         * shared libraries - for now assume that this
                         * is an a.out format binary
                         */

                        elf_interpreter = (char *) kmalloc(elf_ppnt->p_filesz,
                                                           GFP_KERNEL);
                        if (elf_interpreter == NULL) {
                                kfree (elf_phdata);
                                sys_close(elf_exec_fileno);
                                return -ENOMEM;
                        }

                        retval = read_exec(bprm->inode,elf_ppnt->p_offset,
                                           elf_interpreter,
                                           elf_ppnt->p_filesz, 1);
                        /* If the program interpreter is one of these two,
                           then assume an iBCS2 image. Otherwise assume
                           a native linux image. */
                        if (strcmp(elf_interpreter,"/usr/lib/libc.so.1") == 0 ||
                            strcmp(elf_interpreter,"/usr/lib/ld.so.1") == 0)
                          ibcs2_interpreter = 1;
#if 0
                        printk("Using ELF interpreter %s\n", elf_interpreter);
#endif
                        if (retval >= 0) {
                                old_fs = get_fs(); /* This could probably be optimized */
                                set_fs(get_ds());
                                retval = open_namei(elf_interpreter, 0, 0,
                                                    &interpreter_inode, NULL);
                                set_fs(old_fs);
                        }

                        if (retval >= 0)
                                retval = read_exec(interpreter_inode,0,bprm->buf,128, 1);

                        if (retval >= 0) {
                                interp_ex = *((struct exec *) bprm->buf);               /* exec-header */
                                interp_elf_ex = *((struct elfhdr *) bprm->buf);   /* exec-header */

                        }
                        if (retval < 0) {
                                kfree (elf_phdata);
                                kfree(elf_interpreter);
                                sys_close(elf_exec_fileno);
                                return retval;
                        }
                }
                elf_ppnt++;
        }

        /* Some simple consistency checks for the interpreter */
        if (elf_interpreter){
                interpreter_type = INTERPRETER_ELF | INTERPRETER_AOUT;

                /* Now figure out which format our binary is */
                if ((N_MAGIC(interp_ex) != OMAGIC) &&
                    (N_MAGIC(interp_ex) != ZMAGIC) &&
                    (N_MAGIC(interp_ex) != QMAGIC))
                  interpreter_type = INTERPRETER_ELF;

                if (interp_elf_ex.e_ident[0] != 0x7f ||
                    strncmp(&interp_elf_ex.e_ident[1], "ELF",3) != 0)
                  interpreter_type &= ~INTERPRETER_ELF;

                if (!interpreter_type)
                  {
                    kfree(elf_interpreter);
                    kfree(elf_phdata);
                    sys_close(elf_exec_fileno);
                    return -ELIBBAD;
                  }
        }

        /* OK, we are done with that, now set up the arg stuff,
           and then start this sucker up */

        if (!bprm->sh_bang) {
                char * passed_p;

                if (interpreter_type == INTERPRETER_AOUT) {
                  sprintf(passed_fileno, "%d", elf_exec_fileno);
                  passed_p = passed_fileno;

                  if (elf_interpreter) {
                    bprm->p = copy_strings(1,&passed_p,bprm->page,bprm->p,2);
                    bprm->argc++;
                  }
                }
                if (!bprm->p) {
                        if (elf_interpreter) {
                              kfree(elf_interpreter);
                        }
                        kfree (elf_phdata);
                        sys_close(elf_exec_fileno);
                        return -E2BIG;
                }
        }

        /* OK, This is the point of no return */
        flush_old_exec(bprm);

        current->mm->end_data = 0;
        current->mm->end_code = 0;
        current->mm->start_mmap = ELF_START_MMAP;
        current->mm->mmap = NULL;
        elf_entry = (unsigned long) elf_ex.e_entry;

        /* Do this so that we can load the interpreter, if need be.  We will
           change some of these later */
        current->mm->rss = 0;
        bprm->p = setup_arg_pages(bprm->p, bprm);
        current->mm->start_stack = bprm->p;

        /* Now we do a little grungy work by mmaping the ELF image into
           the correct location in memory.  At this point, we assume that
           the image should be loaded at fixed address, not at a variable
           address. */

        old_fs = get_fs();
        set_fs(get_ds());
        for(i = 0, elf_ppnt = elf_phdata; i < elf_ex.e_phnum; i++, elf_ppnt++) {
                if (elf_ppnt->p_type == PT_LOAD) {
                        int elf_prot = 0;
                        if (elf_ppnt->p_flags & PF_R) elf_prot |= PROT_READ;
                        if (elf_ppnt->p_flags & PF_W) elf_prot |= PROT_WRITE;
                        if (elf_ppnt->p_flags & PF_X) elf_prot |= PROT_EXEC;

                        error = do_mmap(file,
                                        ELF_PAGESTART(elf_ppnt->p_vaddr),
                                        (elf_ppnt->p_filesz +
                                         ELF_PAGEOFFSET(elf_ppnt->p_vaddr)),
                                        elf_prot,
                                        (MAP_FIXED | MAP_PRIVATE |
                                         MAP_DENYWRITE | MAP_EXECUTABLE),
                                        ELF_PAGESTART(elf_ppnt->p_offset));

#ifdef LOW_ELF_STACK
                        if (ELF_PAGESTART(elf_ppnt->p_vaddr) < elf_stack)
                                elf_stack = ELF_PAGESTART(elf_ppnt->p_vaddr);
#endif

                        if (!load_addr)
                          load_addr = elf_ppnt->p_vaddr - elf_ppnt->p_offset;
                        k = elf_ppnt->p_vaddr;
                        if (k < start_code) start_code = k;
                        k = elf_ppnt->p_vaddr + elf_ppnt->p_filesz;
                        if (k > elf_bss) elf_bss = k;
#if 1
                        if ((elf_ppnt->p_flags & PF_X) && end_code <  k)
#else
                        if ( !(elf_ppnt->p_flags & PF_W) && end_code <  k)
#endif
                                end_code = k;
                        if (end_data < k) end_data = k;
                        k = elf_ppnt->p_vaddr + elf_ppnt->p_memsz;
                        if (k > elf_brk) elf_brk = k;
                }
        }
        set_fs(old_fs);

        if (elf_interpreter) {
                if (interpreter_type & 1)
                        elf_entry = load_aout_interp(&interp_ex,
                                                     interpreter_inode);
                else if (interpreter_type & 2)
                        elf_entry = load_elf_interp(&interp_elf_ex,
                                                    interpreter_inode,
                                                    &interp_load_addr);

                iput(interpreter_inode);
                kfree(elf_interpreter);

                if (elf_entry == ~0UL) {
                        printk("Unable to load interpreter\n");
                        kfree(elf_phdata);
                        send_sig(SIGSEGV, current, 0);
                        return 0;
                }
        }

        kfree(elf_phdata);

        if (interpreter_type != INTERPRETER_AOUT) sys_close(elf_exec_fileno);
        current->personality = (ibcs2_interpreter ? PER_SVR4 : PER_LINUX);

        if (current->exec_domain && current->exec_domain->use_count)
                (*current->exec_domain->use_count)--;
        if (current->binfmt && current->binfmt->use_count)
                (*current->binfmt->use_count)--;
        current->exec_domain = lookup_exec_domain(current->personality);
        current->binfmt = &elf_format;
        if (current->exec_domain && current->exec_domain->use_count)
                (*current->exec_domain->use_count)++;
        if (current->binfmt && current->binfmt->use_count)
                (*current->binfmt->use_count)++;

#ifndef VM_STACK_FLAGS
        current->executable = bprm->inode;
        bprm->inode->i_count++;
#endif
#ifdef LOW_ELF_STACK
        current->start_stack = bprm->p = elf_stack - 4;
#endif
        current->suid = current->euid = current->fsuid = bprm->e_uid;
        current->sgid = current->egid = current->fsgid = bprm->e_gid;
        current->flags &= ~PF_FORKNOEXEC;
        bprm->p = (unsigned long)
          create_elf_tables((char *)bprm->p,
                        bprm->argc,
                        bprm->envc,
                        (interpreter_type == INTERPRETER_ELF ? &elf_ex : NULL),
                        load_addr,
                        interp_load_addr,
                        (interpreter_type == INTERPRETER_AOUT ? 0 : 1));
        if (interpreter_type == INTERPRETER_AOUT)
          current->mm->arg_start += strlen(passed_fileno) + 1;
        current->mm->start_brk = current->mm->brk = elf_brk;
        current->mm->end_code = end_code;
#if (SIX)
        current->mm->start_code = start_code;
#else
        current->mm->start_code = start_code;
#endif
        current->mm->end_data = end_data;
        current->mm->start_stack = bprm->p;

        /* Calling set_brk effectively mmaps the pages that we need for the bss and break
           sections */
        set_brk(elf_bss, elf_brk);
        padzero(elf_bss);

#if 0
        printk("(start_brk) %x\n" , current->mm->start_brk);
        printk("(end_code) %x\n" , current->mm->end_code);
        printk("(start_code) %x\n" , current->mm->start_code);
        printk("(end_data) %x\n" , current->mm->end_data);
        printk("(start_stack) %x\n" , current->mm->start_stack);
        printk("(brk) %x\n" , current->mm->brk);
#endif

        if ( current->personality == PER_SVR4 )
        {
                /* Why this, you ask???  Well SVr4 maps page 0 as read-only,
                   and some applications "depend" upon this behavior.
                   Since we do not have the power to recompile these, we
                   emulate the SVr4 behavior.  Sigh.  */
                error = do_mmap(NULL, 0, 4096, PROT_READ | PROT_EXEC,
                                MAP_FIXED | MAP_PRIVATE, 0);
        }

#ifdef ELF_PLAT_INIT
        /*
         * The ABI may specify that certain registers be set up in special
         * ways (on i386 %edx is the address of a DT_FINI function, for
         * example.  This macro performs whatever initialization to
         * the regs structure is required.
         */
        ELF_PLAT_INIT(regs);
#endif


        start_thread(regs, elf_entry, bprm->p);
        if (current->flags & PF_PTRACED)
                send_sig(SIGTRAP, current, 0);
        return 0;
}

static int
load_elf_binary(struct linux_binprm * bprm, struct pt_regs * regs)
{
        int retval;

        MOD_INC_USE_COUNT;
#if (SIX)
	retval = load_six_elf_binary(bprm, regs);
#else
        retval = do_load_elf_binary(bprm, regs);
#endif
        MOD_DEC_USE_COUNT;
        return retval;
}

/*
 * Actual dumper
 *      
 * This is a two-pass process; first we find the offsets of the bits,
 * and then they are actually written out.  If we run out of core limit
 * we just truncate.
 */     
static int elf_core_dump(long signr, struct pt_regs * regs)
{       

}

/*
 * Nothing much here - Just a simple function which allocates a block of
 * size (2*PAGE_SIZE). This is used only in a couple of places; One - while
 * execing and Two - while doing a fork. 
 */
unsigned long alloc_stack()
{
	return  __get_free_pages(GFP_KERNEL, 1, 0);
}

/*
 *	The Memory Layout of an executable in SIX :
 *	+-------------------------------+
 *	|M				|
 *	|				|
 *	|	   SIX Stack		|--> This is the solaris stack of SIX 
 *	|				|
 *	|				|
 *	+-------------------------------+ 0xefffc000
 *	|L				|
 *	|				|
 *	.				.
 *	.				.
 *	.				.
 *	.      				.
 *	.     Various libraries like	.
 *	.	  Libelf. So		.
 *	.	  Libdl.so		.
 *	.	  libc.so		.
 *	.         .......		.
 *	.				.
 *	.				.
 *	.				.
 *	.				.
 *	|				|
 *	|				|
 *	+-------------------------------+ 0x10000000
 *	|K				|
 *	|				|
 *	|      Executable Stack		|
 *	|				|
 *	|				|
 *	+-------------------------------+  -
 *	|J				|  |
 *	|				|  |
 *	|				|  |
 *	| 	     HOLE		|  |--> Heap can grow and eat up this much
 *	|				|  |    But - this is a lot. Quarter of a GB.
 *	|				|  |
 *	|	       			|  |
 *	+-------------------------------+  -
 *	|I				|
 *	|				|
 *	|      Executable Heap		|
 *	|				|
 *	|				|
 *	+-------------------------------+
 *	|H				|
 *	|				|
 *	|      Executable Data		|
 *	|				|
 *	|				|
 *	+-------------------------------+
 *	|G				|
 *	|				|
 *	|      Executable Text		|
 *	|				|
 *	|				|
 *	+-------------------------------+ 0x3000000      
 *	|F				|		 
 *	|	     HOLE		|		 
 *	|				|		 
 *	+-------------------------------+		-
 *	|E				|		|
 *	|				|		|
 *	|	           		|		|
 *	|				|		|
 *	|           SIX RAM		|		|---> 32 MB, say
 *	|				|		|
 *	|				|		|
 *	|				|		|
 *	|				|		|
 *	+-------------------------------+  -		-
 *	|D				|  |
 *	|				|  |
 *	|	'GAP' or 'HOLE'		|  |--> around 20 pages approx
 *	|				|  |
 *	|				|  |
 *	+-------------------------------+  -
 *	|C				| 
 *	|	    SIX Heap		|
 *	|				|
 *	+-------------------------------+
 *	|B				|
 *	|	    SIX Data		|
 *	|				|
 *	+-------------------------------+
 *	|A				|
 *	|	    SIX Code		|
 *	|				|
 *	+-------------------------------+ 0x10000
 *	|				|
 *	|				|
 *	+-------------------------------+ 0x0
 *
 *
 * When SIX begins to Boot up, it uses the Solaris stack (M). The code that gets executed
 * is the one lying in (A). Remember that (A) contains the whole of the kernel code. Soon
 * the end of start_kernel() is reached, and a kernel_task() of init() is called; This
 * creates the init() kernel thread, which has its own stack. Remember that this stack space
 * comes from the mmapped 'ram' part. Which is (E). All memory requirements, including the
 * stacks of all kernel threads and user processes are satisfied from this segment, (E).
 * So in other words, once init() is spawned the stack in (M) is hardly used anymore; Unless
 * there is a need for the idle thread to be brought in.
 *
 * Now init() goes about its work, spawning off bdflushd and kswapd. So far all the code that
 * executed is from segment (A). Soon the init() thread loads up /etc/init from the
 * root disk. The executable is copied into the ram space (E). When its about to be scheduled,
 * its code data and stack, lying inside (E), is mapped to the space between 0x3000000 and
 * 0x4000000. The stack grows down from 0x4000000. The text starts from 0x3000000 and the 
 * data, right after it. When it gets scheduled, execution switches over to (G).
 *
 * Now each time a timer interrupt comes in, execution switches off to sun_handler() which
 * is part of the kernel code -  I.e., part of (A). After completing its work, it switches the
 * context back to (G). The case is similar when a system call happens.
 *
 */

static inline int
do_six_load_elf_binary(struct linux_binprm * bprm, struct pt_regs *regs) {
	int retval, size, ret, i;
	long tbase = 0, tsize = 0, dsize = 0, total = 0;
	unsigned long entry;
	char *str;

	struct vm_area_struct *stack_vm = 0;

	Elf32_Shdr *ss[4], *s, *strhdr;
	Elf32_Ehdr *e;

	ss[0] = ss[1] = ss[2] = ss[3] = 0;
	e = (char *)kmalloc(sizeof(Elf32_Ehdr), GFP_KERNEL);
	s = (char *)kmalloc(sizeof(Elf32_Shdr), GFP_KERNEL);
	strhdr = (char *)kmalloc(sizeof(Elf32_Shdr), GFP_KERNEL);

	/*
	 * Read the Elf header
	 */
        read_exec(bprm->inode, 0, e, sizeof(Elf32_Ehdr), 1);
	/*
	 * Read the String header
	 */
        read_exec(bprm->inode, e->e_shoff+e->e_shentsize*e->e_shstrndx, 
		 strhdr, sizeof(Elf32_Shdr), 1);
	str = (char *)kmalloc(strhdr->sh_size, GFP_KERNEL);
	/*
	 * Read the String table.
	 */
        read_exec(bprm->inode, strhdr->sh_offset,
		 str, strhdr->sh_size, 1);


	/*
	 * Parse through the elf image, and read in the section headers. If one is either
	 * a text, data, bss or rodata header, then store it.
	 */
        for(i=0; i<e->e_shnum; i++)
        {
        	read_exec(bprm->inode, e->e_shoff+i*e->e_shentsize,
		 	s, e->e_shentsize, 1);
                if(!strcmp(str+(s->sh_name), ".text"))
		{
                        ss[TEXT] = s;
#if 0
			printk("text: at %d, size %d\n", 
				ss[TEXT]->sh_addr, ss[TEXT]->sh_size);
#endif
			s = (char *)kmalloc(sizeof(Elf32_Shdr), GFP_KERNEL);
			tbase = downclick(ss[TEXT]->sh_addr);
			if(tsize) /* we already found the .rodata */
				tsize = (ss[RODATA]->sh_addr + ss[RODATA]->sh_size) - tbase;
			else
				tsize = (ss[TEXT]->sh_addr + ss[TEXT]->sh_size) - tbase;
			continue;
		}
                if(!strcmp(str+(s->sh_name), ".data"))
		{
                        ss[DATA] = s;
#if 0
			printk("data: at %d, size %d\n", 
				downclick(ss[DATA]->sh_addr), ss[1]->sh_size);
#endif
			s = (char *)kmalloc(sizeof(Elf32_Shdr), GFP_KERNEL);
			if(dsize)
				dsize = (ss[BSS]->sh_addr + ss[BSS]->sh_size) - ss[1]->sh_addr;
			else
				dsize = ss[DATA]->sh_size;
			continue;
		}
                if(!strcmp(str+(s->sh_name), ".rodata"))
		{
                        ss[RODATA] = s;
#if 0
			printk("rodata: at %d, size %d\n", 
				ss[RODATA]->sh_addr, ss[RODATA]->sh_size);
#endif
			s = (char *)kmalloc(sizeof(Elf32_Shdr), GFP_KERNEL);
			if(!tbase)
				tbase = downclick(ss[RODATA]->sh_addr);
			tsize = (ss[RODATA]->sh_addr + ss[RODATA]->sh_size) - tbase;
			continue;
		}
                if(!strcmp(str+(s->sh_name), ".bss"))
		{
                        ss[BSS] = s;
#if 0
			printk("bss: at %d, size %d\n", 
				ss[BSS]->sh_addr, ss[BSS]->sh_size);
#endif
			s = (char *)kmalloc(sizeof(Elf32_Shdr), GFP_KERNEL);
			if(dsize)
				dsize = (ss[BSS]->sh_addr + ss[BSS]->sh_size) - ss[DATA]->sh_addr;
			else
				dsize = ss[BSS]->sh_size;
			continue;
		}
        }
	
	if (!tbase) /* exec failed */
		return 0;
	
	if (ss[BSS])
		total = (ss[BSS]->sh_addr + ss[BSS]->sh_size) - tbase;
	else if (ss[1])
		total = (ss[DATA]->sh_addr + ss[DATA]->sh_size) - tbase;
	else /* no .data or .bss - just .text and/or .rodata! */
		total = tsize;

	total = upclick(total);
	
	/* Ok this is a point of no return, too! */
	flush_old_exec(bprm);

	if(!current->is_mapped)
	{
		/*
		 * This guy is a kernel_thread which is holding on to a 2 page stack
		 * allocated through alloc_stack(). We dont need those 2 pages anymore,
		 * since soon an mmap is going to bring us a fresh stack, mapped in place.
		 */
		free_pages(current->mm->start_stack, 1);
		current->is_mapped = 1;
	}

	/*
	 * Create mappings for text, rodata, data and bss
	 */
	retval  =  do_mmap(0, tbase, total, PROT_READ | PROT_WRITE | PROT_EXEC,
		   MAP_PRIVATE | MAP_FIXED, 0);

	if (retval != tbase)
	{
		printf("ERROR: load_binary: mmap failed (%d)\n", retval);
		return 0;
	}

	retval = do_mmap(0, STACK_BASE - DEFAULT_STACK_SIZE, DEFAULT_STACK_SIZE,
	   	 PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_FIXED, 0);

	if (retval != (STACK_BASE - DEFAULT_STACK_SIZE))
	{
		printf("ERROR: load_binary: stack mmap failed (%d)\n", retval);
		return 0;
	}
	/*
	 * Wipe the slate clean!
	 */
	memset(STACK_BASE - DEFAULT_STACK_SIZE, 0, DEFAULT_STACK_SIZE);

	current->mm->start_code = tbase; 
	current->mm->end_code =  tbase + tsize;
	if(ss[DATA])
	{
		current->mm->start_data = ss[1]->sh_addr;
		current->mm->end_data = current->mm->start_data + dsize;
	}
	current->mm->start_brk = tbase + total;
	current->mm->brk = current->mm->start_brk;
	current->mm->start_stack = STACK_BASE - DEFAULT_STACK_SIZE;

	current->_sigreturn = 0;
	current->one = current->two = current->three = 0;

	regs->uc_sp_size = DEFAULT_STACK_SIZE - 8;
#if (!__i386__)
	/*
	 * Leave space for a double word. (thanks to smx guys)
	 */
	regs->uc_sp = STACK_BASE - 8;
#else
	/*
	 * Im clueless abt this one; for some strange reason, makecontext()
	 * on x86 behaves wierd - you need to point ss_sp to the beginning of
	 * the stack area. Unlike the sparc scenario, where you have to point
	 * it to the end, from where it will grown down, before calling 
	 * makecontext(). Have you ever noticed how easy it is to commit
	 * silly changes once you add proper comments? :) its almost like
	 * this - stupidity is not a bad thing, if you are aware of its
	 * existence, approximate measure etc!
	 */
	regs->uc_sp = STACK_BASE - DEFAULT_STACK_SIZE;
#endif


	/*
	 * read in the text, data, bss and rodata segments from the disk.
	 */
	if(ss[TEXT])
	{
        	read_exec(bprm->inode, ss[TEXT]->sh_offset,
		ss[TEXT]->sh_addr, ss[TEXT]->sh_size, 1);
		kfree(ss[TEXT]);
	}
	if(ss[DATA])
	{
        	read_exec(bprm->inode, ss[DATA]->sh_offset,
		ss[DATA]->sh_addr, ss[DATA]->sh_size, 1);
		kfree(ss[DATA]);
	}
	if(ss[RODATA])
	{
        	read_exec(bprm->inode, ss[RODATA]->sh_offset,
		ss[RODATA]->sh_addr, ss[RODATA]->sh_size, 1);
		kfree(ss[RODATA]);
	}
	if(ss[BSS])
	{
		/*
		 * This is bss - uninitialized data - which should
		 * be all set to zeroes. So ask some zeroes to go live there.
		 *
		 * Two bugs lived in these four lines.  The kfree() came
		 * first, so the section header was read after it had been
		 * returned to the allocator.  And the arguments were in the
		 * order (address, size, fill) rather than memset's actual
		 * (address, fill, size), so the call asked to write zero
		 * bytes of the value sh_size -- that is, it did nothing at
		 * all, and .bss was left holding whatever happened to be in
		 * the freshly mapped pages.
		 */
		memset((void *)ss[BSS]->sh_addr, 0, ss[BSS]->sh_size);
		kfree(ss[BSS]);
	}

	/*
	 * The beginning of the page in bprm->page[0] contains an initial list
	 * of addresses which point to strings in argv. Then comes a null, and
	 * then another list which points to strings in envp.
	 *
	 * Take the entry point out of the ELF header *before* freeing it.
	 * This is the bug that kept the x86 port from ever running a guest:
	 * the original freed `e' and then read e->e_entry three lines later,
	 * so the address control was transferred to was whatever the
	 * allocator had since put in that memory.  On Solaris in 2005 kfree()
	 * evidently left the bytes alone and it worked by luck; this kernel's
	 * allocator reuses the block immediately, and the guest was being
	 * entered at a garbage address -- which is exactly what it looked
	 * like from outside: the exec succeeds, control leaves the kernel,
	 * and the guest never executes a single system call.
	 */
	entry = e->e_entry;

	/*
	 * Clean up
	 */
	kfree(e);
	kfree(strhdr);
	kfree(str);

	/*
	 * cstart(argc, argv, envp) -- three arguments, not four.  makecontext
	 * reads exactly as many as it is told to, so asking for four made it
	 * copy one word past the end of the argument list onto the new stack.
	 */
	makecontext(regs, entry, 3, bprm->argc,
			(char **)bprm->page[0],
			(char **)(bprm->page[0]+(bprm->argc+1)*4));

	/*
	 * From here on this task is running guest code, and that has to be
	 * recorded, because it is how system_call() knows where to look for
	 * a system call's arguments: a guest leaves them in its own memory
	 * and passes the address in %esi, while the kernel's own callers use
	 * the six_call global.  See include/asm-six/sixcall.h.
	 *
	 * go_user_mode() has existed in include/asm-six/ptrace.h since 2003
	 * and was never called from anywhere.  It did not need to be on
	 * SPARC, where the arguments arrive in real registers that the host
	 * saves into the ucontext whoever the caller was, so there was
	 * nothing for the kernel to decide.  On x86 there are two channels
	 * and the flag is the only thing that distinguishes them.
	 *
	 * Leaving it unset produced a very good disguise.  The guest would
	 * start, make its first system call, and system_call() would read
	 * six_call -- which still held the arguments of the last call made
	 * through that channel, namely init()'s own execve of /bin/sh.  So
	 * it would exec /bin/sh again.  The guest would start, make its
	 * first system call, and so on.  From the outside it looked like a
	 * program that produced no output.
	 */
	go_user_mode();

	return 0;
}

static int
load_six_elf_binary(struct linux_binprm * bprm, struct pt_regs *regs)
{
        int retval;

        MOD_INC_USE_COUNT;
        retval = do_six_load_elf_binary(bprm, regs);
        MOD_DEC_USE_COUNT;
        return retval;
}

/* This is really simpleminded and specialized - we are loading an
   a.out library that is given an ELF header. */

static inline int
do_load_elf_library(int fd){
}


static int load_elf_library(int fd)
{
        int retval;

        MOD_INC_USE_COUNT;
        retval = do_load_elf_library(fd);
        MOD_DEC_USE_COUNT;
        return retval;
}



int init_elf_binfmt(void) 
{
        return register_binfmt(&elf_format);
}

