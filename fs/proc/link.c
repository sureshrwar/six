
/*
 *  linux/fs/proc/link.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  /proc link-file handling code
 */

#include <asm/segment.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>

static int proc_readlink(struct inode *, char *, int);
static int proc_follow_link(struct inode *, struct inode *, int, int,
			    struct inode **);

/*
 * PLAN9_SEMANTICS won't work any more: it used an ugly hack that broke 
 * when the files[] array was updated only after the open code
 */
#undef PLAN9_SEMANTICS

/*
 * links can't do much...
 */
static struct file_operations proc_fd_link_operations = {
	NULL,			/* lseek - default */
	NULL,			/* read - bad */
	NULL,			/* write - bad */
	NULL,			/* readdir - bad */
	NULL,			/* select - default */
	NULL,			/* ioctl - default */
	NULL,			/* mmap */
	NULL,			/* very special open code */
	NULL,			/* no special release code */
	NULL			/* can't fsync */
};

struct inode_operations proc_link_inode_operations = {
	&proc_fd_link_operations,/* file-operations */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	proc_readlink,		/* readlink */
	proc_follow_link,	/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};


static int proc_follow_link(struct inode * dir, struct inode * inode,
	int flag, int mode, struct inode ** res_inode)
{
	unsigned int pid, ino;
	struct task_struct * p;
	struct inode * new_inode;
	int i, error;

	*res_inode = NULL;
	if (dir)
		iput(dir);
	if (!inode)
		return -ENOENT;
	if ((error = permission(inode, MAY_EXEC)) != 0){
		iput(inode);
		return error;
	}
	ino = inode->i_ino;
	pid = ino >> 16;
	ino &= 0x0000ffff;
	for (i = 0 ; i < NR_TASKS ; i++)
		if ((p = task[i]) && p->pid == pid)
			break;
	if (i >= NR_TASKS) {
		iput(inode);
		return -ENOENT;
	}
	new_inode = NULL;
	switch (ino) {
		case PROC_PID_CWD:
			if (!p->fs)
				break;
			new_inode = p->fs->pwd;
			break;
		case PROC_PID_ROOT:
			if (!p->fs)
				break;
			new_inode = p->fs->root;
			break;
		case PROC_PID_EXE: {
			struct vm_area_struct * vma;
			if (!p->mm)
				break;
			vma = p->mm->mmap;
			while (vma) {
				if (vma->vm_flags & VM_EXECUTABLE) {
					new_inode = vma->vm_inode;
					break;
				}
				vma = vma->vm_next;
			}
			break;
		}
		default:
			switch (ino >> 8) {
			case PROC_PID_FD_DIR:
				if (!p->files)
					break;
				ino &= 0xff;
				if (ino < NR_OPEN && p->files->fd[ino]) {
					new_inode = p->files->fd[ino]->f_inode;
				}
				break;
			}
	}
	iput(inode);
	if (!new_inode)
		return -ENOENT;
	*res_inode = new_inode;
	new_inode->i_count++;
	return 0;
}

static int proc_readlink(struct inode * inode, char * buffer, int buflen)
{
	int i;
	unsigned int dev, ino, orig_ino, pid, low_ino;
	char buf[128];

	if (!S_ISLNK(inode->i_mode)) {
		iput(inode);
		return -EINVAL;
	}
	orig_ino = inode->i_ino;
	pid = orig_ino >> 16;
	low_ino = orig_ino & 0xffff;
#if (SIX)
	if (low_ino == PROC_PID_EXE) {
		extern const char *six_get_task_exe(struct task_struct *p);
		struct task_struct *p = NULL;
		const char *exe;
		for (i = 0; i < NR_TASKS; i++) {
			if (task[i] && task[i]->pid == pid) {
				p = task[i];
				break;
			}
		}
		if (p) {
			exe = six_get_task_exe(p);
			if (exe && exe[0]) {
				iput(inode);
				i = sprintf(buf, "%s", exe);
				if (buflen > i)
					buflen = i;
				for (i = 0; i < buflen; i++)
					put_user(buf[i], buffer++);
				return i;
			} else if (!p->user_mode) {
				iput(inode);
				i = sprintf(buf, "[%s]", p->comm);
				if (buflen > i)
					buflen = i;
				for (i = 0; i < buflen; i++)
					put_user(buf[i], buffer++);
				return i;
			}
		}
	}
#endif
	i = proc_follow_link(NULL, inode, 0, 0, &inode);
	if (i)
		return i;
	if (!inode)
		return -EIO;
	dev = kdev_t_to_nr(inode->i_dev);
	ino = inode->i_ino;
#if (SIX)
	if (current->fs && inode == current->fs->root) {
		i = sprintf(buf, "/");
	} else if (inode->i_sb && inode->i_sb->s_magic == PROC_SUPER_MAGIC) {
		if (ino == PROC_ROOT_INO)
			i = sprintf(buf, "/proc");
		else if ((ino >> 16) != 0 && (ino & 0xffff) == PROC_PID_INO)
			i = sprintf(buf, "/proc/%u", ino >> 16);
		else if ((ino >> 16) != 0 && (ino & 0xffff) == PROC_PID_FD)
			i = sprintf(buf, "/proc/%u/fd", ino >> 16);
		else
			i = sprintf(buf, "[%04x]:%u", dev, ino);
	} else if (S_ISCHR(inode->i_mode) && MAJOR(inode->i_rdev) == 1 && MINOR(inode->i_rdev) == 3) {
		i = sprintf(buf, "/dev/null");
	} else if (S_ISCHR(inode->i_mode) && MAJOR(inode->i_rdev) == 1 && MINOR(inode->i_rdev) == 5) {
		i = sprintf(buf, "/dev/zero");
	} else if (S_ISCHR(inode->i_mode) && MAJOR(inode->i_rdev) == 1 && MINOR(inode->i_rdev) == 1) {
		i = sprintf(buf, "/dev/mem");
	} else if (S_ISCHR(inode->i_mode) && MAJOR(inode->i_rdev) == 4) {
		i = sprintf(buf, "/dev/tty%d", MINOR(inode->i_rdev));
	} else if (S_ISCHR(inode->i_mode) && MAJOR(inode->i_rdev) == 2) {
		i = sprintf(buf, "/dev/ptyp%d", MINOR(inode->i_rdev));
	} else if (S_ISCHR(inode->i_mode) && MAJOR(inode->i_rdev) == 3) {
		i = sprintf(buf, "/dev/ttyp%d", MINOR(inode->i_rdev));
	} else if (S_ISCHR(inode->i_mode) && MAJOR(inode->i_rdev) == 10 && MINOR(inode->i_rdev) == 229) {
		i = sprintf(buf, "/dev/fuse");
	} else if (S_ISCHR(inode->i_mode) && MAJOR(inode->i_rdev) == 61) {
		i = sprintf(buf, "/dev/sadb");
	} else if (S_ISCHR(inode->i_mode) && MAJOR(inode->i_rdev) == 63) {
		i = sprintf(buf, "/dev/binder");
	} else if (S_ISBLK(inode->i_mode) && MAJOR(inode->i_rdev) == 3) {
		i = sprintf(buf, "/dev/hd%c", MINOR(inode->i_rdev) == 0 ? 'a' : 'b');
	} else {
		i = sprintf(buf, "[%04x]:%u", dev, ino);
	}
#else
	i = sprintf(buf,"[%04x]:%u", dev, ino);
#endif
	iput(inode);
	if (buflen > i)
		buflen = i;
	i = 0;
	while (i < buflen)
		put_user(buf[i++],buffer++);
	return i;
}
