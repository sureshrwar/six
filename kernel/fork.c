
/*
 *  linux/kernel/fork.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 *  'fork.c' contains the help-routines for the 'fork' system call
 * (see also system_call.s).
 * Fork is rather simple, once you get the hang of it, but the memory
 * management can be a bitch. See 'mm/mm.c': 'copy_page_tables()'
 */
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/ldt.h>
#include <linux/smp.h>

#include <asm/segment.h>
#include <asm/system.h>
#include <asm/pgtable.h>

#if (SIX)
#include <six_proc.h>
#endif

int nr_tasks=1;
int nr_running=1;
unsigned long int total_forks=0;        /* Handle normal Linux uptimes. */
int last_pid=0;


static inline int find_empty_process(void)
{
        int i;

        if (nr_tasks >= NR_TASKS - MIN_TASKS_LEFT_FOR_ROOT) {
                if (current->uid)
                        return -EAGAIN;
        }
        if (current->uid) {
                long max_tasks = current->rlim[RLIMIT_NPROC].rlim_cur;

                max_tasks--;    /* count the new process.. */
                if (max_tasks < nr_tasks) {
                        struct task_struct *p;
                        for_each_task (p) {
                                if (p->uid == current->uid)
                                        if (--max_tasks < 0)
                                                return -EAGAIN;
                        }
                }
        }
        for (i = 0 ; i < NR_TASKS ; i++) {
                if (!task[i])
                        return i;
        }
        return -EAGAIN;
}

static int get_pid(unsigned long flags)
{
        struct task_struct *p;

        if (flags & CLONE_PID)
                return current->pid;
repeat:
        if ((++last_pid) & 0xffff8000)
                last_pid=1;
        for_each_task (p) {
                if (p->pid == last_pid ||
                    p->pgrp == last_pid ||
                    p->session == last_pid)
                        goto repeat;
        }
        return last_pid;
}

static inline int dup_mmap(struct mm_struct * mm)
{
        struct vm_area_struct * mpnt, **p, *tmp;

        mm->mmap = NULL;
        p = &mm->mmap;
	/*
	 * For each vm_area_struct...
	 */
        for (mpnt = current->mm->mmap ; mpnt ; mpnt = mpnt->vm_next)
	{
                tmp = (struct vm_area_struct *) kmalloc(sizeof(struct vm_area_struct),
							 GFP_KERNEL);
                if (!tmp) {
                        exit_mmap(mm);
                        return -ENOMEM;
                }
                *tmp = *mpnt;
                tmp->vm_flags &= ~VM_LOCKED;
                tmp->vm_mm = mm;
                tmp->vm_next = NULL;
                if (tmp->vm_inode) {
                        tmp->vm_inode->i_count++;
                        /* insert tmp into the share list, just after mpnt */
                        tmp->vm_next_share->vm_prev_share = tmp;
                        mpnt->vm_next_share = tmp;
                        tmp->vm_prev_share = mpnt;
                }
                if (tmp->vm_ops && tmp->vm_ops->open)
                        tmp->vm_ops->open(tmp); 
                if (copy_page_range(mm, current->mm, tmp)) {
                        exit_mmap(mm);
                        return -ENOMEM;
                }
                *p = tmp;
                p = &tmp->vm_next;
        }
        build_mmap_avl(mm);
        return 0;
}       

static  int copy_mm(unsigned long clone_flags, struct task_struct * tsk)
{
        if (!(clone_flags & CLONE_VM)) {
                struct mm_struct * mm = kmalloc(sizeof(*tsk->mm), GFP_KERNEL);
                if (!mm)
                        return -1;
                *mm = *current->mm;
                mm->count = 1;
                mm->def_flags = 0;
                tsk->mm = mm;
                tsk->min_flt = tsk->maj_flt = 0;
                tsk->cmin_flt = tsk->cmaj_flt = 0;
                tsk->nswap = tsk->cnswap = 0;
                if (new_page_tables(tsk))
                        return -1;
                if (dup_mmap(mm)) {
                        free_page_tables(mm);
                        return -1;
                }
                return 0;
        }
        SET_PAGE_DIR(tsk, current->mm->pgd);
        current->mm->count++;
        return 0;
}

static inline int copy_fs(unsigned long clone_flags, struct task_struct * tsk)
{
        if (clone_flags & CLONE_FS) {
                current->fs->count++;
                return 0;
        }
        tsk->fs = kmalloc(sizeof(*tsk->fs), GFP_KERNEL);
        if (!tsk->fs)
                return -1;
        tsk->fs->count = 1;
        tsk->fs->umask = current->fs->umask;
        if ((tsk->fs->root = current->fs->root))
                tsk->fs->root->i_count++;
        if ((tsk->fs->pwd = current->fs->pwd))
                tsk->fs->pwd->i_count++;
        return 0;
}

static inline int copy_files(unsigned long clone_flags, struct task_struct * tsk)
{
        int i;
        struct files_struct *oldf, *newf;
        struct file **old_fds, **new_fds;

        oldf = current->files;
        if (clone_flags & CLONE_FILES) {
                oldf->count++;
                return 0;
        }

        newf = kmalloc(sizeof(*newf), GFP_KERNEL);
        tsk->files = newf;
        if (!newf)
                return -1;

        newf->count = 1;
        newf->close_on_exec = oldf->close_on_exec;
        newf->open_fds = oldf->open_fds;

        old_fds = oldf->fd;
        new_fds = newf->fd;
        for (i = NR_OPEN; i != 0; i--) {
                struct file * f = *old_fds;
                old_fds++;
                *new_fds = f;
                new_fds++;
                if (f)
                        f->f_count++;
        }
        return 0;
}

static inline int copy_sighand(unsigned long clone_flags, struct task_struct * tsk)
{
        if (clone_flags & CLONE_SIGHAND) {
                current->sig->count++;
                return 0;
        }
        tsk->sig = kmalloc(sizeof(*tsk->sig), GFP_KERNEL);
        if (!tsk->sig)
                return -1;
        tsk->sig->count = 1;
        memcpy(tsk->sig->action, current->sig->action, sizeof(tsk->sig->action));
        return 0;
}

#if (SIX)
extern unsigned long six_task_saved_pc(struct task_struct *p);
extern int six_host_sprint_symbol(unsigned long addr, char *buf, int buflen);

static char six_task_cmdline[NR_TASKS][128];

void six_set_task_cmdline(struct task_struct *tsk, int argc, char **argv)
{
	int i, slot = -1, pos = 0;

	if (!tsk)
		return;
	for (i = 0; i < NR_TASKS; i++) {
		if (task[i] == tsk) {
			slot = i;
			break;
		}
	}
	if (slot < 0)
		return;

	six_task_cmdline[slot][0] = '\0';
	if (!argv || argc <= 0)
		return;
	for (i = 0; i < argc && pos < 120; i++) {
		const char *arg = argv[i];
		unsigned long a = (unsigned long)arg;
		if (!arg || !((a >= 0x03000000UL && a < TASK_SIZE) || (a >= _ram_start && a < high_memory)))
			break;
		if (i > 0 && pos < 126)
			six_task_cmdline[slot][pos++] = ' ';
		while (*arg && pos < 126)
			six_task_cmdline[slot][pos++] = *arg++;
	}
	six_task_cmdline[slot][pos] = '\0';
}

int six_get_task_cmdline(struct task_struct *p, char *buf)
{
	int i, slot = -1;

	if (!p || !buf)
		return 0;
	for (i = 0; i < NR_TASKS; i++) {
		if (task[i] == p) {
			slot = i;
			break;
		}
	}
	if (!p->user_mode)
		return sprintf(buf, "[%s]\n", p->comm);
	if (slot >= 0 && six_task_cmdline[slot][0])
		return sprintf(buf, "%s\n", six_task_cmdline[slot]);
	return sprintf(buf, "%s\n", p->comm);
}

int sys_sixps(struct six_proc *sp)
{
	int index;
	struct task_struct *p;
	struct vm_area_struct *vma;
	unsigned long vsz = 0;
	char sym[64], *plus, *name;

	if (!sp)
		return -1;
	index = sp->index;
	if (index < 0)
		index = 0;
	while (index < NR_TASKS && !task[index])
		index++;
	if (index >= NR_TASKS || !task[index])
		return -1;

	p = task[index];
	memset(sp, 0, sizeof(*sp));
	strncpy(sp->comm, p->comm, sizeof(sp->comm) - 1);
	sp->pid = p->pid;
	sp->ppid = p->p_pptr ? p->p_pptr->pid : (p->p_opptr ? p->p_opptr->pid : 0);
	sp->uid = p->uid;
	sp->euid = p->euid;
	sp->gid = p->gid;
	sp->pgrp = p->pgrp;
	sp->session = p->session;
	sp->state = (int)p->state;
	sp->utime = (p->pid == 0) ? 0 : p->utime;
	sp->stime = (p->pid == 0) ? 0 : p->stime;
	sp->start_time = p->start_time;
	sp->jiffies_now = jiffies;
	sp->nice = (long)DEF_PRIORITY - p->priority;
	sp->priority = 80 + sp->nice;
	sp->is_kthread = !p->user_mode;

	if (p->tty) {
		int maj = MAJOR(p->tty->device);
		int min = MINOR(p->tty->device);
		sp->tty_nr = (int)p->tty->device;
		if (maj == 4)
			sprintf(sp->tty_name, "tty%d", min);
		else if (maj == 3)
			sprintf(sp->tty_name, "ttyp%d", min);
		else
			sprintf(sp->tty_name, "tty%d,%d", maj, min);
	} else {
		sp->tty_nr = 0;
		strcpy(sp->tty_name, "?");
	}

	if (p->user_mode && p->mm) {
		for (vma = p->mm->mmap; vma; vma = vma->vm_next) {
			if (vma->vm_end > vma->vm_start)
				vsz += (vma->vm_end - vma->vm_start) >> 10;
		}
		if (vsz == 0)
			vsz = 64;
		sp->vsize_kb = vsz;
		sp->rss_kb = (p->mm->rss > 0) ? ((unsigned long)p->mm->rss << (PAGE_SHIFT - 10)) : (vsz / 2 + 16);
		sp->memsize = (int)sp->vsize_kb;
	} else {
		sp->vsize_kb = 0;
		sp->rss_kb = 0;
		sp->memsize = 0;
	}

	if (p->state == TASK_INTERRUPTIBLE || p->state == TASK_UNINTERRUPTIBLE) {
		sp->wchan_addr = six_task_saved_pc(p);
		sym[0] = '\0';
		if (sp->wchan_addr && six_host_sprint_symbol(sp->wchan_addr, sym, sizeof(sym))) {
			for (plus = sym; *plus; plus++) {
				if (*plus == '+') {
					*plus = '\0';
					break;
				}
			}
			name = sym;
			if (strncmp(name, "sys_", 4) == 0)
				name += 4;
			else if (strncmp(name, "six_", 4) == 0)
				name += 4;
			else if (strncmp(name, "__", 2) == 0)
				name += 2;
			strncpy(sp->wchan, name, sizeof(sp->wchan) - 1);
		} else {
			strcpy(sp->wchan, "?");
		}
	} else {
		sp->wchan_addr = 0;
		strcpy(sp->wchan, "-");
	}

	if (!p->user_mode) {
		sprintf(sp->args, "[%s]", p->comm);
	} else if (six_task_cmdline[index][0]) {
		strncpy(sp->args, six_task_cmdline[index], sizeof(sp->args) - 1);
	} else {
		strncpy(sp->args, p->comm, sizeof(sp->args) - 1);
	}

	sp->index = index + 1;
	return 0;
}
#endif

/*
 *  Ok, this is the main fork-routine. It copies the system process
 * information (task[nr]) and sets up the necessary registers. It
 * also copies the data segment in its entirety.
 */
int do_fork(unsigned long clone_flags, unsigned long usp, struct pt_regs *regs)
{

        int nr;
        int error = -ENOMEM;
        unsigned long new_stack;
        struct task_struct *p;

        p = (struct task_struct *) kmalloc(sizeof(*p), GFP_KERNEL);
        if (!p)
                goto bad_fork;
        new_stack = alloc_kernel_stack();
        if (!new_stack)
                goto bad_fork_free_p;
        error = -EAGAIN; 
        nr = find_empty_process();
        if (nr < 0)
                goto bad_fork_free_stack;
#if (SIX)
	{
		int pi;
		six_task_cmdline[nr][0] = '\0';
		for (pi = 0; pi < NR_TASKS; pi++) {
			if (task[pi] == current) {
				strcpy(six_task_cmdline[nr], six_task_cmdline[pi]);
				break;
			}
		}
	}
#endif
	 *p = *current;
        if (p->exec_domain && p->exec_domain->use_count)
                (*p->exec_domain->use_count)++;
        if (p->binfmt && p->binfmt->use_count)
                (*p->binfmt->use_count)++;
        p->did_exec = 0;         
        p->swappable = 0;
        p->kernel_stack_page = new_stack;
        *(unsigned long *) p->kernel_stack_page = STACK_MAGIC;
#if (SIX)
	p->nsp = p->kernel_stack_page + DEFAULT_STACK_SIZE;
#if (!__i386__)
	/*
	 * Leave room for a sparc stack frame
	 */
	p->nsp -= SPARC_FRAME;
#else
	/*
	 * Point to the last word.
	 */
	p->nsp -= 4;
#endif
#endif
        p->state = TASK_UNINTERRUPTIBLE;
        p->flags &= ~(PF_PTRACED|PF_TRACESYS|PF_SUPERPRIV);
        p->flags |= PF_FORKNOEXEC;
        p->pid = get_pid(clone_flags);
        p->next_run = NULL;
        p->prev_run = NULL;
        p->p_pptr = p->p_opptr = current;
        p->p_cptr = NULL;
        p->signal = 0;
        p->it_real_value = p->it_virt_value = p->it_prof_value = 0;
        p->it_real_incr = p->it_virt_incr = p->it_prof_incr = 0;
	init_timer(&p->real_timer);
        p->real_timer.data = (unsigned long) p;
        p->leader = 0;          /* process leadership doesn't inherit */
        p->tty_old_pgrp = 0;
        p->utime = p->stime = 0;
        p->cutime = p->cstime = 0;
#ifdef __SMP__
        p->processor = NO_PROC_ID;
        p->lock_depth = 1;
#endif
        p->start_time = jiffies;
        task[nr] = p;
	SET_LINKS(p);
	nr_tasks++;

	error = -ENOMEM;
        /* copy all the process information */
        if (copy_files(clone_flags, p))
                goto bad_fork_cleanup;
        if (copy_fs(clone_flags, p))
                goto bad_fork_cleanup_files;
        if (copy_sighand(clone_flags, p))
                goto bad_fork_cleanup_fs;
        if (copy_mm(clone_flags, p))
                goto bad_fork_cleanup_sighand;
        copy_thread(nr, clone_flags, usp, p, regs);
        p->semundo = NULL;

        /* ok, now we should be set up.. */
        p->swappable = 1;
        p->exit_signal = clone_flags & CSIGNAL;
        p->counter = current->counter >> 1;
        wake_up_process(p);                     /* do this last, just in case */
        ++total_forks;
        return p->pid;


bad_fork_cleanup_sighand :
	exit_sighand(p);
bad_fork_cleanup_fs :
	exit_fs(p);
bad_fork_cleanup_files :
	exit_files(p);
bad_fork_cleanup:       
        if (p->exec_domain && p->exec_domain->use_count)
                (*p->exec_domain->use_count)--;
        if (p->binfmt && p->binfmt->use_count)
                (*p->binfmt->use_count)--;
        task[nr] = NULL;
        REMOVE_LINKS(p);
        nr_tasks--; 
bad_fork_free_stack:
        free_kernel_stack(new_stack);
bad_fork_free_p	:
	kfree(p);
bad_fork :
	return error;

}
