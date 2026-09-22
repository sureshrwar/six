/*
 * linux/fs/tmpfs/inode.c
 *
 * In-memory tmpfs filesystem for SIX (Linux 2.0.11).
 *
 * Stores regular file data in demand-allocated 4 KB kernel pages
 * (get_free_page / free_page) and supports full POSIX directory/file
 * operations, hard/symbolic links, device/FIFO nodes, sticky-bit (/tmp)
 * semantics, statfs accounting, and demand-paged mmap/execve via
 * generic_file_mmap + tmpfs_readpage.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/pagemap.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/locks.h>
#include <asm/segment.h>

#define TMPFS_SUPER_MAGIC	0x01021994UL
#define TMPFS_ROOT_INO		1UL
#define TMPFS_DEFAULT_MAX_BYTES	(16UL * 1024UL * 1024UL)
#define TMPFS_DEFAULT_INODES	4096UL
#define TMPFS_MAX_FILE_PAGES	4096U	/* 16 MB max per file */
#define TMPFS_NAME_LEN		255

struct tmpfs_dirent {
	unsigned long ino;
	int namelen;
	char name[TMPFS_NAME_LEN + 1];
	struct tmpfs_dirent *next;
};

struct tmpfs_node {
	unsigned long ino;
	unsigned long parent_ino;
	umode_t mode;
	uid_t uid;
	gid_t gid;
	nlink_t nlink;
	kdev_t rdev;
	off_t size;
	time_t atime;
	time_t mtime;
	time_t ctime;
	unsigned long *pages;
	unsigned int cap_pages;
	unsigned int nr_pages;
	char *symlink_target;
	struct tmpfs_dirent *entries;
	struct tmpfs_node *next;
};

struct tmpfs_sb {
	unsigned long max_bytes;
	unsigned long bytes_used;
	unsigned long max_inodes;
	unsigned long nr_inodes;
	unsigned long next_ino;
	umode_t root_mode;
	struct tmpfs_node *nodes;
};

extern struct inode_operations tmpfs_file_inode_operations;
extern struct inode_operations tmpfs_dir_inode_operations;
extern struct inode_operations tmpfs_symlink_inode_operations;

static struct tmpfs_sb *tmpfs_get_sb(struct super_block *sb)
{
	return sb ? (struct tmpfs_sb *)sb->u.generic_sbp : NULL;
}

static struct tmpfs_node *tmpfs_find_node(struct super_block *sb, unsigned long ino)
{
	struct tmpfs_sb *tsb = tmpfs_get_sb(sb);
	struct tmpfs_node *n;

	if (!tsb)
		return NULL;
	for (n = tsb->nodes; n; n = n->next) {
		if (n->ino == ino)
			return n;
	}
	return NULL;
}

static struct tmpfs_node *tmpfs_get_node(struct inode *inode)
{
	struct tmpfs_node *n;

	if (!inode)
		return NULL;
	n = (struct tmpfs_node *)inode->u.generic_ip;
	if (!n && inode->i_sb) {
		n = tmpfs_find_node(inode->i_sb, inode->i_ino);
		inode->u.generic_ip = n;
	}
	return n;
}

static void tmpfs_sync_inode_from_node(struct inode *inode, struct tmpfs_node *n)
{
	if (!inode || !n)
		return;
	inode->i_mode = n->mode;
	inode->i_uid = n->uid;
	inode->i_gid = n->gid;
	inode->i_nlink = n->nlink;
	inode->i_rdev = n->rdev;
	inode->i_size = n->size;
	inode->i_atime = n->atime;
	inode->i_mtime = n->mtime;
	inode->i_ctime = n->ctime;
	inode->i_blksize = PAGE_SIZE;
	inode->i_blocks = (unsigned long)n->nr_pages * (PAGE_SIZE / 512);
	inode->u.generic_ip = n;

	if (S_ISREG(n->mode))
		inode->i_op = &tmpfs_file_inode_operations;
	else if (S_ISDIR(n->mode))
		inode->i_op = &tmpfs_dir_inode_operations;
	else if (S_ISLNK(n->mode))
		inode->i_op = &tmpfs_symlink_inode_operations;
	else if (S_ISCHR(n->mode))
		inode->i_op = &chrdev_inode_operations;
	else if (S_ISBLK(n->mode))
		inode->i_op = &blkdev_inode_operations;
	else if (S_ISFIFO(n->mode))
		init_fifo(inode);
	else
		inode->i_op = NULL;
}

static void tmpfs_sync_node_from_inode(struct inode *inode)
{
	struct tmpfs_node *n = tmpfs_get_node(inode);

	if (!n)
		return;
	n->mode = inode->i_mode;
	n->uid = inode->i_uid;
	n->gid = inode->i_gid;
	n->nlink = inode->i_nlink;
	n->rdev = inode->i_rdev;
	n->size = inode->i_size;
	n->atime = inode->i_atime;
	n->mtime = inode->i_mtime;
	n->ctime = inode->i_ctime;
}

static struct tmpfs_node *tmpfs_alloc_node(struct super_block *sb, umode_t mode, kdev_t rdev)
{
	struct tmpfs_sb *tsb = tmpfs_get_sb(sb);
	struct tmpfs_node *n;

	if (!tsb || tsb->nr_inodes >= tsb->max_inodes)
		return NULL;

	n = (struct tmpfs_node *)kmalloc(sizeof(*n), GFP_KERNEL);
	if (!n)
		return NULL;
	memset(n, 0, sizeof(*n));

	n->ino = tsb->next_ino++;
	n->parent_ino = TMPFS_ROOT_INO;
	n->mode = mode;
	n->uid = current ? current->fsuid : 0;
	n->gid = current ? current->fsgid : 0;
	n->nlink = S_ISDIR(mode) ? 2 : 1;
	n->rdev = rdev;
	n->size = S_ISDIR(mode) ? 64 : 0;
	n->atime = n->mtime = n->ctime = CURRENT_TIME;

	n->next = tsb->nodes;
	tsb->nodes = n;
	tsb->nr_inodes++;
	return n;
}

static void tmpfs_free_node_pages(struct super_block *sb, struct tmpfs_node *n,
				  unsigned int from_page)
{
	struct tmpfs_sb *tsb = tmpfs_get_sb(sb);
	unsigned int i;

	if (!n || !n->pages)
		return;

	for (i = from_page; i < n->cap_pages; i++) {
		if (n->pages[i]) {
			free_page(n->pages[i]);
			n->pages[i] = 0;
			if (n->nr_pages > 0)
				n->nr_pages--;
			if (tsb && tsb->bytes_used >= PAGE_SIZE)
				tsb->bytes_used -= PAGE_SIZE;
		}
	}

	if (from_page == 0) {
		kfree(n->pages);
		n->pages = NULL;
		n->cap_pages = 0;
		n->nr_pages = 0;
	}
}

static void tmpfs_destroy_node(struct super_block *sb, unsigned long ino)
{
	struct tmpfs_sb *tsb = tmpfs_get_sb(sb);
	struct tmpfs_node **pp, *n;
	struct tmpfs_dirent *de, *next_de;

	if (!tsb)
		return;

	for (pp = &tsb->nodes; (n = *pp) != NULL; pp = &n->next) {
		if (n->ino == ino) {
			*pp = n->next;
			tmpfs_free_node_pages(sb, n, 0);
			if (n->symlink_target)
				kfree(n->symlink_target);
			de = n->entries;
			while (de) {
				next_de = de->next;
				kfree(de);
				de = next_de;
			}
			kfree(n);
			if (tsb->nr_inodes > 0)
				tsb->nr_inodes--;
			return;
		}
	}
}

static int tmpfs_ensure_page_table(struct tmpfs_node *n, unsigned int page_idx)
{
	unsigned int new_cap;
	unsigned long *new_pages;

	if (page_idx >= TMPFS_MAX_FILE_PAGES)
		return -EFBIG;
	if (page_idx < n->cap_pages)
		return 0;

	new_cap = n->cap_pages ? n->cap_pages : 64;
	while (new_cap <= page_idx && new_cap < TMPFS_MAX_FILE_PAGES)
		new_cap *= 4;
	if (new_cap > TMPFS_MAX_FILE_PAGES)
		new_cap = TMPFS_MAX_FILE_PAGES;

	new_pages = (unsigned long *)kmalloc(new_cap * sizeof(unsigned long), GFP_KERNEL);
	if (!new_pages)
		return -ENOMEM;
	memset(new_pages, 0, new_cap * sizeof(unsigned long));
	if (n->pages) {
		memcpy(new_pages, n->pages, n->cap_pages * sizeof(unsigned long));
		kfree(n->pages);
	}
	n->pages = new_pages;
	n->cap_pages = new_cap;
	return 0;
}

/*
 * Check sticky bit (S_ISVTX) permission on directory before unlink/rmdir/rename.
 */
static int tmpfs_check_sticky(struct tmpfs_node *dir, struct tmpfs_node *victim)
{
	if (!(dir->mode & S_ISVTX))
		return 0;
	if (suser())
		return 0;
	if (current->fsuid == victim->uid || current->fsuid == dir->uid)
		return 0;
	return -EPERM;
}

/* -------------------------------------------------------------------------
 * Regular file operations
 * ------------------------------------------------------------------------- */

static int tmpfs_file_lseek(struct inode *inode, struct file *file,
			    off_t offset, int origin)
{
	off_t pos;

	switch (origin) {
	case 0:
		pos = offset;
		break;
	case 1:
		pos = file->f_pos + offset;
		break;
	case 2:
		pos = inode->i_size + offset;
		break;
	default:
		return -EINVAL;
	}
	if (pos < 0)
		return -EINVAL;
	file->f_pos = pos;
	file->f_reada = 0;
	file->f_version = ++event;
	return (int)pos;
}

static int tmpfs_file_read(struct inode *inode, struct file *file,
			   char *buf, int count)
{
	struct tmpfs_node *n = tmpfs_get_node(inode);
	off_t pos;
	int read_so_far = 0, err;

	if (!n)
		return -EINVAL;
	if (count <= 0)
		return 0;

	err = verify_area(VERIFY_WRITE, buf, count);
	if (err)
		return err;

	pos = file->f_pos;
	if (pos >= n->size)
		return 0;
	if (pos + count > n->size)
		count = (int)(n->size - pos);

	while (read_so_far < count) {
		unsigned int pidx = (unsigned int)(pos >> PAGE_SHIFT);
		unsigned int poff = (unsigned int)(pos & ~PAGE_MASK);
		unsigned int chunk = PAGE_SIZE - poff;

		if (chunk > (unsigned int)(count - read_so_far))
			chunk = (unsigned int)(count - read_so_far);

		if (pidx < n->cap_pages && n->pages && n->pages[pidx]) {
			memcpy_tofs(buf + read_so_far,
				    (const void *)(n->pages[pidx] + poff),
				    chunk);
		} else {
			/* Sparse hole reads back as zeroes */
			unsigned int k;
			for (k = 0; k < chunk; k++)
				put_user(0, buf + read_so_far + k);
		}

		pos += chunk;
		read_so_far += (int)chunk;
	}

	file->f_pos = pos;
	n->atime = inode->i_atime = CURRENT_TIME;
	return read_so_far;
}

static int tmpfs_file_write(struct inode *inode, struct file *file,
			    const char *buf, int count)
{
	struct super_block *sb = inode ? inode->i_sb : NULL;
	struct tmpfs_sb *tsb = tmpfs_get_sb(sb);
	struct tmpfs_node *n = tmpfs_get_node(inode);
	off_t pos;
	int written = 0, err;

	if (!n || !tsb)
		return -EINVAL;
	if (count <= 0)
		return 0;

	err = verify_area(VERIFY_READ, buf, count);
	if (err)
		return err;

	pos = (file->f_flags & O_APPEND) ? n->size : file->f_pos;

	while (written < count) {
		unsigned int pidx = (unsigned int)(pos >> PAGE_SHIFT);
		unsigned int poff = (unsigned int)(pos & ~PAGE_MASK);
		unsigned int chunk = PAGE_SIZE - poff;

		if (chunk > (unsigned int)(count - written))
			chunk = (unsigned int)(count - written);

		err = tmpfs_ensure_page_table(n, pidx);
		if (err)
			break;

		if (!n->pages[pidx]) {
			unsigned long pg;
			if (tsb->bytes_used + PAGE_SIZE > tsb->max_bytes) {
				err = -ENOSPC;
				break;
			}
			pg = get_free_page(GFP_KERNEL);
			if (!pg) {
				err = -ENOMEM;
				break;
			}
			n->pages[pidx] = pg;
			n->nr_pages++;
			tsb->bytes_used += PAGE_SIZE;
		}

		memcpy_fromfs((void *)(n->pages[pidx] + poff),
			      buf + written, chunk);
		pos += chunk;
		written += (int)chunk;
		if (pos > n->size)
			n->size = pos;
	}

	if (written > 0) {
		file->f_pos = pos;
		inode->i_size = n->size;
		inode->i_blocks = (unsigned long)n->nr_pages * (PAGE_SIZE / 512);
		n->mtime = n->ctime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		invalidate_inode_pages(inode);
	}

	return written ? written : err;
}

static void tmpfs_truncate(struct inode *inode)
{
	struct tmpfs_node *n = tmpfs_get_node(inode);
	unsigned int first_free_page, poff;

	if (!n || !S_ISREG(n->mode))
		return;

	n->size = inode->i_size;
	first_free_page = (unsigned int)((n->size + PAGE_SIZE - 1) >> PAGE_SHIFT);
	poff = (unsigned int)(n->size & ~PAGE_MASK);

	/* Zero out the tail of the partial last page */
	if (poff > 0 && (first_free_page - 1) < n->cap_pages &&
	    n->pages && n->pages[first_free_page - 1]) {
		memset((void *)(n->pages[first_free_page - 1] + poff),
		       0, PAGE_SIZE - poff);
	}

	tmpfs_free_node_pages(inode->i_sb, n, first_free_page);
	inode->i_blocks = (unsigned long)n->nr_pages * (PAGE_SIZE / 512);
	n->mtime = n->ctime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	invalidate_inode_pages(inode);
}

static int tmpfs_readpage(struct inode *inode, struct page *page)
{
	struct tmpfs_node *n = tmpfs_get_node(inode);
	unsigned long addr;
	unsigned int pidx;

	addr = page_address(page);
	page->count++;
	set_bit(PG_locked, &page->flags);
	memset((void *)addr, 0, PAGE_SIZE);

	if (n) {
		pidx = (unsigned int)(page->offset >> PAGE_SHIFT);
		if (pidx < n->cap_pages && n->pages && n->pages[pidx])
			memcpy((void *)addr, (const void *)n->pages[pidx], PAGE_SIZE);
		set_bit(PG_uptodate, &page->flags);
	} else {
		set_bit(PG_error, &page->flags);
	}

	clear_bit(PG_locked, &page->flags);
	wake_up(&page->wait);
	free_page(addr);
	return 0;
}

static int tmpfs_fsync(struct inode *inode, struct file *file)
{
	return 0;
}

static struct file_operations tmpfs_file_operations = {
	tmpfs_file_lseek,	/* lseek */
	tmpfs_file_read,	/* read */
	tmpfs_file_write,	/* write */
	NULL,			/* readdir */
	NULL,			/* select */
	NULL,			/* ioctl */
	generic_file_mmap,	/* mmap */
	NULL,			/* open */
	NULL,			/* release */
	tmpfs_fsync,		/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL			/* revalidate */
};

struct inode_operations tmpfs_file_inode_operations = {
	&tmpfs_file_operations,	/* default_file_ops */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	tmpfs_readpage,		/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	tmpfs_truncate,		/* truncate */
	NULL,			/* permission */
	NULL			/* smap */
};

/* -------------------------------------------------------------------------
 * Directory operations
 * ------------------------------------------------------------------------- */

static struct tmpfs_dirent *tmpfs_find_dirent(struct tmpfs_node *dir,
					      const char *name, int len,
					      struct tmpfs_dirent ***prev_out)
{
	struct tmpfs_dirent **pp, *de;

	if (!dir)
		return NULL;
	for (pp = &dir->entries; (de = *pp) != NULL; pp = &de->next) {
		if (de->namelen == len && memcmp(de->name, name, len) == 0) {
			if (prev_out)
				*prev_out = pp;
			return de;
		}
	}
	return NULL;
}

static int tmpfs_add_dirent(struct tmpfs_node *dir, const char *name, int len,
			    unsigned long ino)
{
	struct tmpfs_dirent *de;

	if (len <= 0 || len > TMPFS_NAME_LEN)
		return -ENAMETOOLONG;

	de = (struct tmpfs_dirent *)kmalloc(sizeof(*de), GFP_KERNEL);
	if (!de)
		return -ENOMEM;

	de->ino = ino;
	de->namelen = len;
	memcpy(de->name, name, len);
	de->name[len] = '\0';
	de->next = dir->entries;
	dir->entries = de;
	dir->mtime = dir->ctime = CURRENT_TIME;
	return 0;
}

static int tmpfs_readdir(struct inode *inode, struct file *filp,
			 void *dirent, filldir_t filldir)
{
	struct tmpfs_node *dir = tmpfs_get_node(inode);
	struct tmpfs_dirent *de;
	off_t idx = 0;

	if (!inode || !S_ISDIR(inode->i_mode) || !dir)
		return -EBADF;

	if (filp->f_pos == 0) {
		if (filldir(dirent, ".", 1, 0, inode->i_ino) < 0)
			return 0;
		filp->f_pos = 1;
	}
	if (filp->f_pos == 1) {
		if (filldir(dirent, "..", 2, 1, dir->parent_ino) < 0)
			return 0;
		filp->f_pos = 2;
	}

	idx = 2;
	for (de = dir->entries; de; de = de->next, idx++) {
		if (idx < filp->f_pos)
			continue;
		if (filldir(dirent, de->name, de->namelen, filp->f_pos, de->ino) < 0)
			return 0;
		filp->f_pos = idx + 1;
	}
	dir->atime = inode->i_atime = CURRENT_TIME;
	return 0;
}

static int tmpfs_lookup(struct inode *dir, const char *name, int len,
			struct inode **result)
{
	struct super_block *sb;
	struct tmpfs_node *dnode;
	struct tmpfs_dirent *de;
	unsigned long ino = 0;

	*result = NULL;
	if (!dir)
		return -ENOENT;
	if (!S_ISDIR(dir->i_mode)) {
		iput(dir);
		return -ENOTDIR;
	}

	sb = dir->i_sb;
	dnode = tmpfs_get_node(dir);
	if (!dnode) {
		iput(dir);
		return -ENOENT;
	}

	if (len == 1 && name[0] == '.') {
		dir->i_count++;
		*result = dir;
		iput(dir);
		return 0;
	}
	if (len == 2 && name[0] == '.' && name[1] == '.') {
		ino = dnode->parent_ino;
	} else {
		de = tmpfs_find_dirent(dnode, name, len, NULL);
		if (de)
			ino = de->ino;
	}

	iput(dir);
	if (!ino)
		return -ENOENT;

	*result = iget(sb, ino);
	if (!*result)
		return -EACCES;
	return 0;
}

static int tmpfs_create(struct inode *dir, const char *name, int len, int mode,
			struct inode **result)
{
	struct super_block *sb;
	struct tmpfs_node *dnode, *n;
	int err;

	*result = NULL;
	if (!dir)
		return -ENOENT;
	sb = dir->i_sb;
	dnode = tmpfs_get_node(dir);
	if (!dnode) {
		iput(dir);
		return -EINVAL;
	}
	if (tmpfs_find_dirent(dnode, name, len, NULL)) {
		iput(dir);
		return -EEXIST;
	}

	n = tmpfs_alloc_node(sb, (mode & 07777) | S_IFREG, 0);
	if (!n) {
		iput(dir);
		return -ENOSPC;
	}
	n->parent_ino = dnode->ino;
	if (dnode->mode & S_ISGID)
		n->gid = dnode->gid;

	err = tmpfs_add_dirent(dnode, name, len, n->ino);
	if (err) {
		tmpfs_destroy_node(sb, n->ino);
		iput(dir);
		return err;
	}
	dir->i_mtime = dir->i_ctime = dnode->mtime;
	iput(dir);

	*result = iget(sb, n->ino);
	if (!*result)
		return -ENOMEM;
	return 0;
}

static int tmpfs_mkdir(struct inode *dir, const char *name, int len, int mode)
{
	struct super_block *sb;
	struct tmpfs_node *dnode, *n;
	int err;

	if (!dir)
		return -ENOENT;
	sb = dir->i_sb;
	dnode = tmpfs_get_node(dir);
	if (!dnode) {
		iput(dir);
		return -EINVAL;
	}
	if (tmpfs_find_dirent(dnode, name, len, NULL)) {
		iput(dir);
		return -EEXIST;
	}

	n = tmpfs_alloc_node(sb, (mode & 07777) | S_IFDIR, 0);
	if (!n) {
		iput(dir);
		return -ENOSPC;
	}
	n->parent_ino = dnode->ino;
	if (dnode->mode & S_ISGID) {
		n->gid = dnode->gid;
		n->mode |= S_ISGID;
	}

	err = tmpfs_add_dirent(dnode, name, len, n->ino);
	if (err) {
		tmpfs_destroy_node(sb, n->ino);
		iput(dir);
		return err;
	}
	dnode->nlink++;
	dir->i_nlink = dnode->nlink;
	dir->i_mtime = dir->i_ctime = dnode->mtime;
	iput(dir);
	return 0;
}

static int tmpfs_rmdir(struct inode *dir, const char *name, int len)
{
	struct super_block *sb;
	struct tmpfs_node *dnode, *target;
	struct tmpfs_dirent **pp, *de;
	struct inode *tinode;
	int err;

	if (!dir)
		return -ENOENT;
	sb = dir->i_sb;
	dnode = tmpfs_get_node(dir);
	if (!dnode) {
		iput(dir);
		return -EINVAL;
	}

	de = tmpfs_find_dirent(dnode, name, len, &pp);
	if (!de) {
		iput(dir);
		return -ENOENT;
	}

	target = tmpfs_find_node(sb, de->ino);
	if (!target || !S_ISDIR(target->mode)) {
		iput(dir);
		return -ENOTDIR;
	}
	if (target->ino == dnode->ino) {
		iput(dir);
		return -EPERM;
	}
	if (target->entries != NULL) {
		iput(dir);
		return -ENOTEMPTY;
	}
	err = tmpfs_check_sticky(dnode, target);
	if (err) {
		iput(dir);
		return err;
	}

	tinode = iget(sb, target->ino);
	if (tinode) {
		if (tinode->i_count > 1) {
			iput(tinode);
			iput(dir);
			return -EBUSY;
		}
		*pp = de->next;
		kfree(de);
		target->nlink = 0;
		tinode->i_nlink = 0;
		if (dnode->nlink > 2)
			dnode->nlink--;
		dir->i_nlink = dnode->nlink;
		dnode->mtime = dnode->ctime = dir->i_mtime = dir->i_ctime = CURRENT_TIME;
		iput(tinode);
	}
	iput(dir);
	return 0;
}

static int tmpfs_unlink(struct inode *dir, const char *name, int len)
{
	struct super_block *sb;
	struct tmpfs_node *dnode, *target;
	struct tmpfs_dirent **pp, *de;
	struct inode *tinode;
	int err;

	if (!dir)
		return -ENOENT;
	sb = dir->i_sb;
	dnode = tmpfs_get_node(dir);
	if (!dnode) {
		iput(dir);
		return -EINVAL;
	}

	de = tmpfs_find_dirent(dnode, name, len, &pp);
	if (!de) {
		iput(dir);
		return -ENOENT;
	}

	target = tmpfs_find_node(sb, de->ino);
	if (!target) {
		iput(dir);
		return -ENOENT;
	}
	if (S_ISDIR(target->mode)) {
		iput(dir);
		return -EPERM;
	}
	err = tmpfs_check_sticky(dnode, target);
	if (err) {
		iput(dir);
		return err;
	}

	*pp = de->next;
	kfree(de);
	dnode->mtime = dnode->ctime = dir->i_mtime = dir->i_ctime = CURRENT_TIME;

	tinode = iget(sb, target->ino);
	if (tinode) {
		if (target->nlink > 0)
			target->nlink--;
		tinode->i_nlink = target->nlink;
		tinode->i_ctime = target->ctime = CURRENT_TIME;
		iput(tinode);
	} else if (target->nlink <= 1) {
		tmpfs_destroy_node(sb, target->ino);
	} else {
		target->nlink--;
	}

	iput(dir);
	return 0;
}

static int tmpfs_link(struct inode *oldinode, struct inode *dir,
		      const char *name, int len)
{
	struct tmpfs_node *dnode, *onode;
	int err;

	if (!dir || !oldinode) {
		if (dir)
			iput(dir);
		if (oldinode)
			iput(oldinode);
		return -ENOENT;
	}
	if (S_ISDIR(oldinode->i_mode)) {
		iput(dir);
		iput(oldinode);
		return -EPERM;
	}

	dnode = tmpfs_get_node(dir);
	onode = tmpfs_get_node(oldinode);
	if (!dnode || !onode) {
		iput(dir);
		iput(oldinode);
		return -EINVAL;
	}
	if (tmpfs_find_dirent(dnode, name, len, NULL)) {
		iput(dir);
		iput(oldinode);
		return -EEXIST;
	}

	err = tmpfs_add_dirent(dnode, name, len, onode->ino);
	if (err) {
		iput(dir);
		iput(oldinode);
		return err;
	}

	onode->nlink++;
	oldinode->i_nlink = onode->nlink;
	onode->ctime = oldinode->i_ctime = CURRENT_TIME;
	dir->i_mtime = dir->i_ctime = dnode->mtime;

	iput(dir);
	iput(oldinode);
	return 0;
}

static int tmpfs_symlink(struct inode *dir, const char *name, int len,
			 const char *symname)
{
	struct super_block *sb;
	struct tmpfs_node *dnode, *n;
	int symlen, err;
	char *target;

	if (!dir)
		return -ENOENT;
	sb = dir->i_sb;
	dnode = tmpfs_get_node(dir);
	if (!dnode) {
		iput(dir);
		return -EINVAL;
	}
	if (tmpfs_find_dirent(dnode, name, len, NULL)) {
		iput(dir);
		return -EEXIST;
	}

	symlen = strlen(symname);
	target = (char *)kmalloc(symlen + 1, GFP_KERNEL);
	if (!target) {
		iput(dir);
		return -ENOMEM;
	}
	memcpy(target, symname, symlen + 1);

	n = tmpfs_alloc_node(sb, S_IFLNK | 0777, 0);
	if (!n) {
		kfree(target);
		iput(dir);
		return -ENOSPC;
	}
	n->parent_ino = dnode->ino;
	n->symlink_target = target;
	n->size = symlen;

	err = tmpfs_add_dirent(dnode, name, len, n->ino);
	if (err) {
		tmpfs_destroy_node(sb, n->ino);
		iput(dir);
		return err;
	}
	dir->i_mtime = dir->i_ctime = dnode->mtime;
	iput(dir);
	return 0;
}

static int tmpfs_mknod(struct inode *dir, const char *name, int len,
		       int mode, int rdev)
{
	struct super_block *sb;
	struct tmpfs_node *dnode, *n;
	int err;

	if (!dir)
		return -ENOENT;
	sb = dir->i_sb;
	dnode = tmpfs_get_node(dir);
	if (!dnode) {
		iput(dir);
		return -EINVAL;
	}
	if (tmpfs_find_dirent(dnode, name, len, NULL)) {
		iput(dir);
		return -EEXIST;
	}

	n = tmpfs_alloc_node(sb, mode, (kdev_t)rdev);
	if (!n) {
		iput(dir);
		return -ENOSPC;
	}
	n->parent_ino = dnode->ino;
	if (dnode->mode & S_ISGID)
		n->gid = dnode->gid;

	err = tmpfs_add_dirent(dnode, name, len, n->ino);
	if (err) {
		tmpfs_destroy_node(sb, n->ino);
		iput(dir);
		return err;
	}
	dir->i_mtime = dir->i_ctime = dnode->mtime;
	iput(dir);
	return 0;
}

static int tmpfs_rename(struct inode *old_dir, const char *old_name, int old_len,
			struct inode *new_dir, const char *new_name, int new_len,
			int must_be_dir)
{
	struct super_block *sb = old_dir ? old_dir->i_sb : NULL;
	struct tmpfs_node *odnode, *ndnode, *src, *dst = NULL;
	struct tmpfs_dirent **old_pp, *old_de, **new_pp, *new_de;
	int err;

	if (!old_dir || !new_dir || !sb) {
		if (old_dir)
			iput(old_dir);
		if (new_dir)
			iput(new_dir);
		return -EINVAL;
	}

	odnode = tmpfs_get_node(old_dir);
	ndnode = tmpfs_get_node(new_dir);
	if (!odnode || !ndnode) {
		iput(old_dir);
		iput(new_dir);
		return -EINVAL;
	}

	old_de = tmpfs_find_dirent(odnode, old_name, old_len, &old_pp);
	if (!old_de) {
		iput(old_dir);
		iput(new_dir);
		return -ENOENT;
	}

	src = tmpfs_find_node(sb, old_de->ino);
	if (!src) {
		iput(old_dir);
		iput(new_dir);
		return -ENOENT;
	}
	if (must_be_dir && !S_ISDIR(src->mode)) {
		iput(old_dir);
		iput(new_dir);
		return -ENOTDIR;
	}

	err = tmpfs_check_sticky(odnode, src);
	if (err) {
		iput(old_dir);
		iput(new_dir);
		return err;
	}

	new_de = tmpfs_find_dirent(ndnode, new_name, new_len, &new_pp);
	if (new_de) {
		struct inode *dinode;

		if (new_de == old_de) {
			iput(old_dir);
			iput(new_dir);
			return 0;
		}
		dst = tmpfs_find_node(sb, new_de->ino);
		if (!dst) {
			iput(old_dir);
			iput(new_dir);
			return -ENOENT;
		}
		err = tmpfs_check_sticky(ndnode, dst);
		if (err) {
			iput(old_dir);
			iput(new_dir);
			return err;
		}
		if (S_ISDIR(src->mode) && !S_ISDIR(dst->mode)) {
			iput(old_dir);
			iput(new_dir);
			return -ENOTDIR;
		}
		if (!S_ISDIR(src->mode) && S_ISDIR(dst->mode)) {
			iput(old_dir);
			iput(new_dir);
			return -EISDIR;
		}
		if (S_ISDIR(dst->mode) && dst->entries != NULL) {
			iput(old_dir);
			iput(new_dir);
			return -ENOTEMPTY;
		}

		/* Replace existing directory entry */
		new_de->ino = src->ino;
		*old_pp = old_de->next;
		kfree(old_de);

		dinode = iget(sb, dst->ino);
		if (dinode) {
			if (S_ISDIR(dst->mode)) {
				dst->nlink = 0;
				dinode->i_nlink = 0;
				if (ndnode->nlink > 2)
					ndnode->nlink--;
			} else {
				if (dst->nlink > 0)
					dst->nlink--;
				dinode->i_nlink = dst->nlink;
			}
			iput(dinode);
		} else if (dst->nlink <= 1 || S_ISDIR(dst->mode)) {
			tmpfs_destroy_node(sb, dst->ino);
		} else {
			dst->nlink--;
		}
	} else {
		err = tmpfs_add_dirent(ndnode, new_name, new_len, src->ino);
		if (err) {
			iput(old_dir);
			iput(new_dir);
			return err;
		}
		/* Re-find old_de in case odnode == ndnode and head changed */
		old_de = tmpfs_find_dirent(odnode, old_name, old_len, &old_pp);
		if (old_de) {
			*old_pp = old_de->next;
			kfree(old_de);
		}
	}

	if (S_ISDIR(src->mode) && odnode != ndnode) {
		src->parent_ino = ndnode->ino;
		if (odnode->nlink > 2)
			odnode->nlink--;
		ndnode->nlink++;
		old_dir->i_nlink = odnode->nlink;
		new_dir->i_nlink = ndnode->nlink;
	}

	odnode->mtime = odnode->ctime = old_dir->i_mtime = old_dir->i_ctime = CURRENT_TIME;
	ndnode->mtime = ndnode->ctime = new_dir->i_mtime = new_dir->i_ctime = CURRENT_TIME;
	iput(old_dir);
	iput(new_dir);
	return 0;
}

static struct file_operations tmpfs_dir_operations = {
	NULL,			/* lseek */
	NULL,			/* read */
	NULL,			/* write */
	tmpfs_readdir,		/* readdir */
	NULL,			/* select */
	NULL,			/* ioctl */
	NULL,			/* mmap */
	NULL,			/* open */
	NULL,			/* release */
	tmpfs_fsync,		/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL			/* revalidate */
};

struct inode_operations tmpfs_dir_inode_operations = {
	&tmpfs_dir_operations,	/* default_file_ops */
	tmpfs_create,		/* create */
	tmpfs_lookup,		/* lookup */
	tmpfs_link,		/* link */
	tmpfs_unlink,		/* unlink */
	tmpfs_symlink,		/* symlink */
	tmpfs_mkdir,		/* mkdir */
	tmpfs_rmdir,		/* rmdir */
	tmpfs_mknod,		/* mknod */
	tmpfs_rename,		/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL			/* smap */
};

/* -------------------------------------------------------------------------
 * Symlink operations
 * ------------------------------------------------------------------------- */

static int tmpfs_readlink(struct inode *inode, char *buffer, int buflen)
{
	struct tmpfs_node *n = tmpfs_get_node(inode);
	const char *link;
	int i = 0;

	if (!inode || !S_ISLNK(inode->i_mode) || !n || !n->symlink_target) {
		if (inode)
			iput(inode);
		return -EINVAL;
	}
	link = n->symlink_target;
	while (i < buflen && link[i]) {
		put_user(link[i], buffer + i);
		i++;
	}
	n->atime = inode->i_atime = CURRENT_TIME;
	iput(inode);
	return i;
}

static int tmpfs_follow_link(struct inode *dir, struct inode *inode,
			     int flag, int mode, struct inode **res_inode)
{
	struct tmpfs_node *n = tmpfs_get_node(inode);
	int err;

	*res_inode = NULL;
	if (!dir) {
		dir = current->fs->root;
		dir->i_count++;
	}
	if (!inode) {
		iput(dir);
		return -ENOENT;
	}
	if (!S_ISLNK(inode->i_mode)) {
		iput(dir);
		*res_inode = inode;
		return 0;
	}
	if (current->link_count > 5) {
		iput(dir);
		iput(inode);
		return -ELOOP;
	}
	if (!n || !n->symlink_target) {
		iput(dir);
		iput(inode);
		return -EIO;
	}

	n->atime = inode->i_atime = CURRENT_TIME;
	current->link_count++;
	err = open_namei(n->symlink_target, flag, mode, res_inode, dir);
	current->link_count--;
	iput(inode);
	return err;
}

struct inode_operations tmpfs_symlink_inode_operations = {
	NULL,			/* default_file_ops */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	tmpfs_readlink,		/* readlink */
	tmpfs_follow_link,	/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL			/* smap */
};

/* -------------------------------------------------------------------------
 * Superblock operations & registration
 * ------------------------------------------------------------------------- */

static void tmpfs_read_inode(struct inode *inode)
{
	struct tmpfs_node *n = tmpfs_find_node(inode->i_sb, inode->i_ino);

	if (!n) {
		inode->i_mode = 0;
		return;
	}
	tmpfs_sync_inode_from_node(inode, n);
}

static void tmpfs_write_inode(struct inode *inode)
{
	tmpfs_sync_node_from_inode(inode);
	inode->i_dirt = 0;
}

static int tmpfs_notify_change(struct inode *inode, struct iattr *attr)
{
	int err;

	err = inode_change_ok(inode, attr);
	if (err)
		return err;
	inode_setattr(inode, attr);
	tmpfs_sync_node_from_inode(inode);
	return 0;
}

static void tmpfs_put_inode(struct inode *inode)
{
	if (inode->i_nlink) {
		tmpfs_sync_node_from_inode(inode);
		return;
	}
	inode->i_size = 0;
	tmpfs_truncate(inode);
	tmpfs_destroy_node(inode->i_sb, inode->i_ino);
	clear_inode(inode);
}

static void tmpfs_put_super(struct super_block *sb)
{
	struct tmpfs_sb *tsb = tmpfs_get_sb(sb);

	lock_super(sb);
	if (tsb) {
		while (tsb->nodes)
			tmpfs_destroy_node(sb, tsb->nodes->ino);
		kfree(tsb);
		sb->u.generic_sbp = NULL;
	}
	sb->s_dev = 0;
	unlock_super(sb);
}

static void tmpfs_statfs(struct super_block *sb, struct statfs *buf, int bufsiz)
{
	struct tmpfs_sb *tsb = tmpfs_get_sb(sb);
	struct statfs tmp;
	unsigned long total_blocks = 0, used_blocks = 0, free_blocks = 0;

	memset(&tmp, 0, sizeof(tmp));
	tmp.f_type = TMPFS_SUPER_MAGIC;
	tmp.f_bsize = PAGE_SIZE;
	tmp.f_namelen = TMPFS_NAME_LEN;

	if (tsb) {
		total_blocks = tsb->max_bytes >> PAGE_SHIFT;
		used_blocks = tsb->bytes_used >> PAGE_SHIFT;
		free_blocks = (total_blocks > used_blocks) ? (total_blocks - used_blocks) : 0;
		tmp.f_blocks = (long)total_blocks;
		tmp.f_bfree = (long)free_blocks;
		tmp.f_bavail = (long)free_blocks;
		tmp.f_files = (long)tsb->max_inodes;
		tmp.f_ffree = (long)((tsb->max_inodes > tsb->nr_inodes)
				     ? (tsb->max_inodes - tsb->nr_inodes) : 0);
	}

	memcpy_tofs(buf, &tmp, bufsiz);
}

static struct super_operations tmpfs_sops = {
	tmpfs_read_inode,	/* read_inode */
	tmpfs_notify_change,	/* notify_change */
	tmpfs_write_inode,	/* write_inode */
	tmpfs_put_inode,	/* put_inode */
	tmpfs_put_super,	/* put_super */
	NULL,			/* write_super */
	tmpfs_statfs,		/* statfs */
	NULL			/* remount_fs */
};

static unsigned long tmpfs_parse_size(const char *s, int base)
{
	unsigned long val = 0;

	while (*s) {
		int d = -1;
		if (*s >= '0' && *s <= '9')
			d = *s - '0';
		else if (base == 8 && *s >= '0' && *s <= '7')
			d = *s - '0';
		if (d < 0 || d >= base)
			break;
		val = val * (unsigned long)base + (unsigned long)d;
		s++;
	}
	if (*s == 'k' || *s == 'K')
		val *= 1024UL;
	else if (*s == 'm' || *s == 'M')
		val *= 1024UL * 1024UL;
	return val;
}

static void tmpfs_parse_options(char *options, struct tmpfs_sb *tsb)
{
	char *p = options;

	if (!p)
		return;
	while (*p) {
		char *next = strchr(p, ',');
		if (next)
			*next = '\0';

		if (!strncmp(p, "size=", 5)) {
			unsigned long sz = tmpfs_parse_size(p + 5, 10);
			if (sz >= PAGE_SIZE)
				tsb->max_bytes = (sz + PAGE_SIZE - 1) & PAGE_MASK;
		} else if (!strncmp(p, "mode=", 5)) {
			tsb->root_mode = (umode_t)tmpfs_parse_size(p + 5, 8) & 07777;
		} else if (!strncmp(p, "nr_inodes=", 10)) {
			unsigned long ni = tmpfs_parse_size(p + 10, 10);
			if (ni >= 16)
				tsb->max_inodes = ni;
		}

		if (next) {
			*next = ',';
			p = next + 1;
		} else {
			break;
		}
	}
}

static struct super_block *tmpfs_read_super(struct super_block *sb,
					    void *data, int silent)
{
	struct tmpfs_sb *tsb;
	struct tmpfs_node *root_node;
	struct inode *root_inode;

	tsb = (struct tmpfs_sb *)kmalloc(sizeof(*tsb), GFP_KERNEL);
	if (!tsb)
		return NULL;
	memset(tsb, 0, sizeof(*tsb));
	tsb->max_bytes = TMPFS_DEFAULT_MAX_BYTES;
	tsb->max_inodes = TMPFS_DEFAULT_INODES;
	tsb->next_ino = TMPFS_ROOT_INO;
	tsb->root_mode = 01777;

	tmpfs_parse_options((char *)data, tsb);

	lock_super(sb);
	sb->s_magic = TMPFS_SUPER_MAGIC;
	sb->s_blocksize = PAGE_SIZE;
	sb->s_blocksize_bits = PAGE_SHIFT;
	sb->s_op = &tmpfs_sops;
	sb->u.generic_sbp = tsb;
	unlock_super(sb);

	root_node = tmpfs_alloc_node(sb, S_IFDIR | tsb->root_mode, 0);
	if (!root_node) {
		kfree(tsb);
		sb->u.generic_sbp = NULL;
		return NULL;
	}
	root_node->uid = 0;
	root_node->gid = 0;
	root_node->parent_ino = TMPFS_ROOT_INO;

	root_inode = iget(sb, TMPFS_ROOT_INO);
	if (!root_inode) {
		tmpfs_put_super(sb);
		return NULL;
	}

	sb->s_mounted = root_inode;
	return sb;
}

static struct file_system_type tmpfs_fs_type = {
	tmpfs_read_super, "tmpfs", 0, NULL
};

static struct file_system_type ramfs_fs_type = {
	tmpfs_read_super, "ramfs", 0, NULL
};

int init_tmpfs_fs(void)
{
	register_filesystem(&tmpfs_fs_type);
	register_filesystem(&ramfs_fs_type);
	return 0;
}
