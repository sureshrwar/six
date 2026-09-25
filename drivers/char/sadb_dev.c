/*
 * linux/drivers/char/sadb_dev.c
 *
 * SIX Android Debug Bridge (sadb / sadbd) transport driver for Linux 2.0.11.
 *
 * Character device: /dev/sadb (major 61, minor 0)
 * Procfs status:    /proc/sadb
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <linux/fcntl.h>
#include <linux/sadb.h>
#include <asm/segment.h>
#include <asm/system.h>

#include "../../arch/six/kernel/host.h"

struct sadb_slot {
	int in_use;
	int host_fd;
	int owner_pid;
	unsigned long bytes_rx;
	unsigned long bytes_tx;
};

static struct sadb_slot sadb_slots[SADB_MAX_SLOTS];
static struct wait_queue *sadb_wait = NULL;
static int sadb_host_port = 0;
static char sadb_serial[32] = "emulator-5554";
static unsigned long sadb_total_conns = 0;

void sadb_dev_poll(void)
{
	int i;

	if (!sadb_wait)
		return;

	if (six_host_sadb_poll_accept() > 0) {
		wake_up_interruptible(&sadb_wait);
		return;
	}

	for (i = 0; i < SADB_MAX_SLOTS; i++) {
		if (sadb_slots[i].in_use && sadb_slots[i].host_fd >= 0) {
			if (six_host_net_poll_readable(sadb_slots[i].host_fd) != 0) {
				wake_up_interruptible(&sadb_wait);
				return;
			}
		}
	}
}

int sadb_get_active_host_fds(int *fds_out, int max_fds)
{
	int i, n = 0;
	for (i = 0; i < SADB_MAX_SLOTS && n < max_fds; i++) {
		if (sadb_slots[i].in_use && sadb_slots[i].host_fd >= 0)
			fds_out[n++] = sadb_slots[i].host_fd;
	}
	return n;
}

static int sadb_open(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}

static void sadb_release(struct inode *inode, struct file *file)
{
	long slot_plus_one = (long)file->private_data;
	int slot_id = (int)slot_plus_one - 1;

	if (slot_id >= 0 && slot_id < SADB_MAX_SLOTS) {
		if (sadb_slots[slot_id].in_use) {
			if (sadb_slots[slot_id].host_fd >= 0) {
				six_host_net_close(sadb_slots[slot_id].host_fd);
				sadb_slots[slot_id].host_fd = -1;
			}
			sadb_slots[slot_id].in_use = 0;
			sadb_slots[slot_id].owner_pid = 0;
		}
		file->private_data = NULL;
	}
}

static int sadb_read(struct inode *inode, struct file *file, char *buf, int count)
{
	long slot_plus_one = (long)file->private_data;
	int slot_id = (int)slot_plus_one - 1;
	struct sadb_slot *slot;
	char kbuf[4096];
	int to_read, r, err;

	if (slot_id < 0 || slot_id >= SADB_MAX_SLOTS || !sadb_slots[slot_id].in_use)
		return -ENOTCONN;
	if (count <= 0)
		return 0;

	err = verify_area(VERIFY_WRITE, buf, count);
	if (err)
		return err;

	slot = &sadb_slots[slot_id];
	to_read = count < (int)sizeof(kbuf) ? count : (int)sizeof(kbuf);

	for (;;) {
		if (slot->host_fd < 0)
			return 0;

		r = six_host_sadb_recv(slot->host_fd, kbuf, to_read);
		if (r > 0) {
			memcpy_tofs(buf, kbuf, r);
			slot->bytes_rx += r;
			return r;
		}
		if (r == 0 || r == -1) {
			/* EOF or socket reset */
			return 0;
		}
		/* r == -2: EAGAIN / would block */
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (current->signal & ~current->blocked)
			return -EINTR;
		interruptible_sleep_on(&sadb_wait);
	}
}

static int sadb_write(struct inode *inode, struct file *file, const char *buf, int count)
{
	long slot_plus_one = (long)file->private_data;
	int slot_id = (int)slot_plus_one - 1;
	struct sadb_slot *slot;
	char kbuf[4096];
	int written = 0;
	int err;

	if (slot_id < 0 || slot_id >= SADB_MAX_SLOTS || !sadb_slots[slot_id].in_use)
		return -ENOTCONN;
	if (count <= 0)
		return 0;

	err = verify_area(VERIFY_READ, buf, count);
	if (err)
		return err;

	slot = &sadb_slots[slot_id];
	while (written < count) {
		int chunk = count - written;
		int r;
		if (chunk > (int)sizeof(kbuf))
			chunk = sizeof(kbuf);
		memcpy_fromfs(kbuf, buf + written, chunk);
		r = six_host_sadb_send_all(slot->host_fd, kbuf, chunk);
		if (r <= 0)
			return written > 0 ? written : -EPIPE;
		written += r;
		slot->bytes_tx += r;
		if (r < chunk)
			break;
	}
	return written;
}

static int sadb_select(struct inode *inode, struct file *file, int sel_type, select_table *wait)
{
	long slot_plus_one = (long)file->private_data;
	int slot_id = (int)slot_plus_one - 1;

	if (sel_type == SEL_OUT)
		return 1;

	if (sel_type == SEL_IN) {
		if (slot_id < 0) {
			if (six_host_sadb_poll_accept() > 0)
				return 1;
			select_wait(&sadb_wait, wait);
			return 0;
		}
		if (slot_id < SADB_MAX_SLOTS && sadb_slots[slot_id].in_use) {
			int hfd = sadb_slots[slot_id].host_fd;
			if (hfd < 0 || six_host_net_poll_readable(hfd) != 0)
				return 1;
			select_wait(&sadb_wait, wait);
			return 0;
		}
		return 1;
	}
	return 0;
}

static int sadb_ioctl(struct inode *inode, struct file *file,
		      unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case SADB_IOC_ACCEPT: {
		int cfd, i;

		if (file->private_data != NULL)
			return -EISCONN;

		for (;;) {
			cfd = six_host_sadb_accept();
			if (cfd >= 0)
				break;
			if (file->f_flags & O_NONBLOCK)
				return -EAGAIN;
			if (current->signal & ~current->blocked)
				return -EINTR;
			interruptible_sleep_on(&sadb_wait);
		}

		for (i = 0; i < SADB_MAX_SLOTS; i++) {
			if (!sadb_slots[i].in_use) {
				sadb_slots[i].in_use = 1;
				sadb_slots[i].host_fd = cfd;
				sadb_slots[i].owner_pid = current ? current->pid : 0;
				sadb_slots[i].bytes_rx = 0;
				sadb_slots[i].bytes_tx = 0;
				file->private_data = (void *)(long)(i + 1);
				sadb_total_conns++;
				return i;
			}
		}
		six_host_net_close(cfd);
		return -EMFILE;
	}

	case SADB_IOC_GET_SERIAL: {
		int err = verify_area(VERIFY_WRITE, (void *)arg, 32);
		if (err)
			return err;
		memcpy_tofs((void *)arg, sadb_serial, 32);
		return 0;
	}

	case SADB_IOC_GET_PORT:
		return sadb_host_port;

	default:
		return -EINVAL;
	}
}

static struct file_operations sadb_fops = {
	NULL,		/* lseek */
	sadb_read,	/* read */
	sadb_write,	/* write */
	NULL,		/* readdir */
	sadb_select,	/* select */
	sadb_ioctl,	/* ioctl */
	NULL,		/* mmap */
	sadb_open,	/* open */
	sadb_release,	/* release */
	NULL,		/* fsync */
	NULL,		/* fasync */
	NULL,		/* check_media_change */
	NULL		/* revalidate */
};

int sadb_get_proc_info(char *buf)
{
	int len = 0;
	int i, active = 0;

	for (i = 0; i < SADB_MAX_SLOTS; i++) {
		if (sadb_slots[i].in_use)
			active++;
	}

	len += sprintf(buf + len,
		       "SIX Android Debug Bridge (sadb) Transport:\n"
		       "  device:            /dev/sadb (char %d:%d)\n"
		       "  serial:            %s\n"
		       "  host_tcp_port:     127.0.0.1:%d\n"
		       "  host_unix_socket:  .six_sadb.sock\n"
		       "  total_connections: %lu\n"
		       "  active_sessions:   %d / %d\n",
		       SADB_CHAR_MAJOR, SADB_CHAR_MINOR,
		       sadb_serial, sadb_host_port,
		       sadb_total_conns, active, SADB_MAX_SLOTS);

	for (i = 0; i < SADB_MAX_SLOTS; i++) {
		if (sadb_slots[i].in_use) {
			len += sprintf(buf + len,
				       "    slot[%d]: pid=%d rx=%lu tx=%lu\n",
				       i, sadb_slots[i].owner_pid,
				       sadb_slots[i].bytes_rx,
				       sadb_slots[i].bytes_tx);
		}
	}
	return len;
}

int sadb_dev_init(void)
{
	memset(sadb_slots, 0, sizeof(sadb_slots));
	if (register_chrdev(SADB_CHAR_MAJOR, "sadb", &sadb_fops)) {
		printk("sadb: unable to register char major %d\n", SADB_CHAR_MAJOR);
		return -EIO;
	}
	six_host_sadb_init(&sadb_host_port, sadb_serial, sizeof(sadb_serial));
	printk("sadb: SIX Debug Bridge initialized (/dev/sadb %d:%d, serial=%s, host=127.0.0.1:%d)\n",
	       SADB_CHAR_MAJOR, SADB_CHAR_MINOR, sadb_serial, sadb_host_port);
	return 0;
}
