/*
 * linux/drivers/char/binder.c
 *
 * Android Binder Inter-Process Communication (IPC) driver for SIX (Linux 2.0.11).
 *
 * Character device: /dev/binder (major 10, minor 58)
 * Procfs status:    /proc/binder
 *
 * Architecture:
 *   - Handle 0 is reserved for the Context Manager (/bin/servicemanager).
 *   - Each process opening /dev/binder gets a `struct binder_proc` associated
 *     with its file descriptor, complete with a per-process incoming transaction
 *     queue (`todo`) and wait queue (`wait`) supporting both blocking `ioctl()`
 *     and `select()`.
 *   - Every Binder service registered with `servicemanager` is assigned a unique
 *     kernel handle (1..N) bound to its owning `struct binder_proc`.
 *   - When a client invokes a transaction on `target_handle`, the kernel:
 *       1. Stamps unforgeable `sender_pid` (current->pid) and `sender_euid`
 *          (current->euid) onto the transaction header.
 *       2. Enqueues the transaction on `target_proc->todo` and wakes up
 *          `target_proc->wait`.
 *       3. For synchronous RPCs (!(flags & TF_ONE_WAY)), blocks the caller on
 *          `txn->reply_wait` until `target_proc` issues `BINDER_IOC_REPLY`
 *          (or `BC_REPLY`), then copies the reply Parcel back to the caller.
 *       4. For asynchronous callbacks (flags & TF_ONE_WAY, e.g. IVoldListener),
 *          returns immediately after queueing.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <linux/binder.h>
#include <asm/segment.h>
#include <asm/system.h>

#define BINDER_MAX_PROCS	32

struct binder_txn {
	unsigned int txn_id;
	struct binder_proc *sender_proc;
	struct binder_proc *target_proc;
	struct binder_ipc_msg msg;
	int done;
	int delivered;
	struct wait_queue *reply_wait;
	struct binder_txn *next;
};

struct binder_proc {
	int in_use;
	int pid;
	int uid;
	char comm[16];
	int max_threads;
	int is_context_mgr;
	unsigned int txns_sent;
	unsigned int txns_received;
	unsigned int last_uevent_seq;
	struct wait_queue *wait;
	struct binder_txn *todo;
	struct binder_txn *in_flight;
};

struct binder_svc_entry {
	int in_use;
	int handle;
	int owner_pid;
	int owner_uid;
	struct binder_proc *owner_proc;
	unsigned int txn_count;
	char name[BINDER_MAX_NAME_LEN];
	char descriptor[BINDER_MAX_DESC_LEN];
};

static struct binder_proc binder_procs[BINDER_MAX_PROCS];
static struct binder_svc_entry binder_svcs[BINDER_MAX_SERVICES];
static struct binder_proc *binder_context_mgr = NULL;
static unsigned int next_txn_id = 1;
static int next_svc_handle = 1;

static unsigned long stat_total_txns = 0;
static unsigned long stat_sync_txns = 0;
static unsigned long stat_oneway_txns = 0;
static unsigned long stat_replies = 0;

#define BINDER_UEVENT_RING_SIZE		8
static struct binder_uevent_msg uevent_ring[BINDER_UEVENT_RING_SIZE];
static unsigned int global_uevent_seq = 1000;

extern void usb_sd_set_online(int online, const char *label, const char *uuid,
			      const char *fstype);
extern void usb_sd_get_meta(int *online, char *label, char *uuid, char *fstype,
			    unsigned long *sectors);

static struct binder_proc *binder_get_proc(struct file *filp)
{
	return filp ? (struct binder_proc *)filp->private_data : NULL;
}

static struct binder_svc_entry *binder_find_svc_by_name(const char *name)
{
	int i;

	if (!name || !name[0])
		return NULL;
	for (i = 0; i < BINDER_MAX_SERVICES; i++) {
		if (binder_svcs[i].in_use &&
		    strcmp(binder_svcs[i].name, name) == 0)
			return &binder_svcs[i];
	}
	return NULL;
}

static struct binder_svc_entry *binder_find_svc_by_handle(int handle)
{
	int i;

	if (handle <= 0)
		return NULL;
	for (i = 0; i < BINDER_MAX_SERVICES; i++) {
		if (binder_svcs[i].in_use &&
		    binder_svcs[i].handle == handle)
			return &binder_svcs[i];
	}
	return NULL;
}

static int binder_register_service_internal(struct binder_proc *proc,
					    const char *name,
					    const char *descriptor,
					    int owner_pid,
					    int owner_uid)
{
	struct binder_svc_entry *svc;
	int i;

	if (!name || !name[0])
		return -EINVAL;

	/* Update existing service if re-registered */
	svc = binder_find_svc_by_name(name);
	if (svc) {
		svc->owner_pid = owner_pid ? owner_pid : (proc ? proc->pid : current->pid);
		svc->owner_uid = owner_uid ? owner_uid : (proc ? proc->uid : current->euid);
		if (proc)
			svc->owner_proc = proc;
		if (descriptor && descriptor[0]) {
			strncpy(svc->descriptor, descriptor, BINDER_MAX_DESC_LEN - 1);
			svc->descriptor[BINDER_MAX_DESC_LEN - 1] = '\0';
		}
		return svc->handle;
	}

	for (i = 0; i < BINDER_MAX_SERVICES; i++) {
		if (!binder_svcs[i].in_use) {
			svc = &binder_svcs[i];
			memset(svc, 0, sizeof(*svc));
			svc->in_use = 1;
			svc->handle = next_svc_handle++;
			svc->owner_pid = owner_pid ? owner_pid : (proc ? proc->pid : current->pid);
			svc->owner_uid = owner_uid ? owner_uid : (proc ? proc->uid : current->euid);
			svc->owner_proc = proc;
			strncpy(svc->name, name, BINDER_MAX_NAME_LEN - 1);
			svc->name[BINDER_MAX_NAME_LEN - 1] = '\0';
			if (descriptor && descriptor[0]) {
				strncpy(svc->descriptor, descriptor, BINDER_MAX_DESC_LEN - 1);
				svc->descriptor[BINDER_MAX_DESC_LEN - 1] = '\0';
			} else {
				strcpy(svc->descriptor, "android.os.IBinder");
			}
			return svc->handle;
		}
	}
	return -ENOMEM;
}

static struct binder_proc *binder_find_proc_by_pid(int pid)
{
	int i;

	for (i = 0; i < BINDER_MAX_PROCS; i++) {
		if (binder_procs[i].in_use && binder_procs[i].pid == pid)
			return &binder_procs[i];
	}
	return NULL;
}

static int binder_open(struct inode *inode, struct file *filp)
{
	int i;
	struct binder_proc *proc = NULL;

	if (MINOR(inode->i_rdev) != BINDER_CHAR_MINOR)
		return -ENXIO;

	for (i = 0; i < BINDER_MAX_PROCS; i++) {
		if (!binder_procs[i].in_use) {
			proc = &binder_procs[i];
			break;
		}
	}
	if (!proc)
		return -ENOMEM;

	memset(proc, 0, sizeof(*proc));
	proc->in_use = 1;
	proc->pid = current->pid;
	proc->uid = current->euid;
	strncpy(proc->comm, current->comm, sizeof(proc->comm) - 1);
	proc->comm[sizeof(proc->comm) - 1] = '\0';
	proc->max_threads = 15;
	proc->last_uevent_seq = global_uevent_seq;
	filp->private_data = proc;
	return 0;
}

static void binder_release(struct inode *inode, struct file *filp)
{
	struct binder_proc *proc = binder_get_proc(filp);
	struct binder_txn *t, *next;
	int i;

	if (!proc)
		return;

	if (binder_context_mgr == proc)
		binder_context_mgr = NULL;

	/* Fail any queued or in-flight transactions targeting this dying proc */
	for (t = proc->todo; t; t = next) {
		next = t->next;
		if (t->msg.flags & TF_ONE_WAY) {
			kfree(t);
		} else {
			t->msg.status = -EPIPE;
			t->done = 1;
			wake_up_interruptible(&t->reply_wait);
		}
	}
	proc->todo = NULL;

	for (t = proc->in_flight; t; t = next) {
		next = t->next;
		t->msg.status = -EPIPE;
		t->done = 1;
		wake_up_interruptible(&t->reply_wait);
	}
	proc->in_flight = NULL;

	/* Unbind any services owned by this proc */
	for (i = 0; i < BINDER_MAX_SERVICES; i++) {
		if (binder_svcs[i].in_use && binder_svcs[i].owner_proc == proc) {
			binder_svcs[i].in_use = 0;
			binder_svcs[i].owner_proc = NULL;
		}
	}

	proc->in_use = 0;
	filp->private_data = NULL;
}

static int binder_select(struct inode *inode, struct file *filp,
			 int sel_type, select_table *wait)
{
	struct binder_proc *proc = binder_get_proc(filp);

	if (!proc)
		return 0;
	if (sel_type == SEL_IN) {
		if (proc->todo != NULL)
			return 1;
		select_wait(&proc->wait, wait);
	}
	return 0;
}

static void binder_enqueue_txn(struct binder_proc *target, struct binder_txn *txn)
{
	struct binder_txn **pp = &target->todo;

	txn->next = NULL;
	while (*pp)
		pp = &(*pp)->next;
	*pp = txn;
	wake_up_interruptible(&target->wait);
}

static int binder_do_transact(struct binder_proc *sender,
			      struct binder_ipc_msg *umsg)
{
	struct binder_ipc_msg kmsg;
	struct binder_proc *target = NULL;
	struct binder_svc_entry *svc = NULL;
	struct binder_txn *txn;
	int err;

	err = verify_area(VERIFY_WRITE, umsg, sizeof(struct binder_ipc_msg));
	if (err)
		return err;
	memcpy_fromfs(&kmsg, umsg, sizeof(kmsg));

	/* Kernel stamps unforgeable caller identity */
	kmsg.sender_pid = current->pid;
	kmsg.sender_euid = current->euid;
	kmsg.status = 0;

	if (kmsg.target_handle == BINDER_CONTEXT_MGR_HANDLE) {
		target = binder_context_mgr;
		/*
		 * Also update the kernel service table for SVC_MGR_ADD_SERVICE
		 * so the newly allocated handle points directly to the caller's
		 * binder_proc!
		 */
		if (kmsg.code == SVC_MGR_ADD_SERVICE && kmsg.data_size > 0) {
			int h = binder_register_service_internal(
				sender, kmsg.data, kmsg.interface_token,
				kmsg.sender_pid, kmsg.sender_euid);
			if (h > 0)
				kmsg.reply_handle = h;
		}
	} else {
		svc = binder_find_svc_by_handle(kmsg.target_handle);
		if (!svc)
			return -ENOENT;
		svc->txn_count++;
		target = svc->owner_proc;
		if (!target && svc->owner_pid > 0)
			target = binder_find_proc_by_pid(svc->owner_pid);
	}

	if (!target || !target->in_use)
		return -ECONNREFUSED;

	/* Prevent self-deadlock if a process synchronously calls itself */
	if (target == sender && !(kmsg.flags & TF_ONE_WAY))
		return -EDEADLK;

	txn = (struct binder_txn *)kmalloc(sizeof(struct binder_txn), GFP_KERNEL);
	if (!txn)
		return -ENOMEM;
	memset(txn, 0, sizeof(*txn));
	txn->txn_id = next_txn_id++;
	kmsg.txn_id = txn->txn_id;
	txn->sender_proc = sender;
	txn->target_proc = target;
	txn->msg = kmsg;

	stat_total_txns++;
	if (sender)
		sender->txns_sent++;
	target->txns_received++;

	if (kmsg.flags & TF_ONE_WAY) {
		stat_oneway_txns++;
		binder_enqueue_txn(target, txn);
		memcpy_tofs(umsg, &kmsg, sizeof(kmsg));
		return 0;
	}

	stat_sync_txns++;
	binder_enqueue_txn(target, txn);

	/* Sleep until target_proc replies or exits */
	while (!txn->done) {
		if (current->signal & ~current->blocked) {
			kfree(txn);
			return -ERESTARTSYS;
		}
		interruptible_sleep_on(&txn->reply_wait);
	}

	memcpy_tofs(umsg, &txn->msg, sizeof(txn->msg));
	err = txn->msg.status;
	kfree(txn);
	return err;
}

static int binder_do_recv(struct binder_proc *proc,
			  struct binder_ipc_msg *umsg,
			  int nonblock)
{
	struct binder_txn *txn;
	int err;

	err = verify_area(VERIFY_WRITE, umsg, sizeof(struct binder_ipc_msg));
	if (err)
		return err;

	while (proc->todo == NULL) {
		if (nonblock)
			return -EAGAIN;
		if (current->signal & ~current->blocked)
			return -ERESTARTSYS;
		interruptible_sleep_on(&proc->wait);
	}

	txn = proc->todo;
	proc->todo = txn->next;
	txn->delivered = 1;

	memcpy_tofs(umsg, &txn->msg, sizeof(txn->msg));

	if (txn->msg.flags & TF_ONE_WAY) {
		kfree(txn);
	} else {
		/* Keep in `in_flight` until BINDER_IOC_REPLY matches `txn_id` */
		txn->next = proc->in_flight;
		proc->in_flight = txn;
	}
	return 0;
}

static int binder_do_reply(struct binder_proc *proc,
			   struct binder_ipc_msg *umsg)
{
	struct binder_ipc_msg kmsg;
	struct binder_txn **pp, *txn = NULL;
	int err;

	err = verify_area(VERIFY_READ, umsg, sizeof(struct binder_ipc_msg));
	if (err)
		return err;
	memcpy_fromfs(&kmsg, umsg, sizeof(kmsg));

	for (pp = &proc->in_flight; *pp; pp = &(*pp)->next) {
		if ((*pp)->txn_id == kmsg.txn_id || kmsg.txn_id == 0) {
			txn = *pp;
			*pp = txn->next;
			break;
		}
	}
	if (!txn)
		return -ENOENT;

	txn->msg.status = kmsg.status;
	txn->msg.reply_handle = kmsg.reply_handle;
	txn->msg.data_size = kmsg.data_size;
	if (kmsg.data_size > 0 && kmsg.data_size <= BINDER_MAX_DATA_SIZE)
		memcpy(txn->msg.data, kmsg.data, kmsg.data_size);
	txn->done = 1;
	stat_replies++;
	wake_up_interruptible(&txn->reply_wait);
	return 0;
}

static int binder_ioctl(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg)
{
	struct binder_proc *proc = binder_get_proc(filp);
	int err;

	if (!proc)
		return -EBADF;

	switch (cmd) {
	case BINDER_VERSION: {
		struct binder_version ver;
		err = verify_area(VERIFY_WRITE, (void *)arg, sizeof(ver));
		if (err)
			return err;
		ver.protocol_version = BINDER_CURRENT_PROTOCOL_VERSION;
		memcpy_tofs((void *)arg, &ver, sizeof(ver));
		return 0;
	}

	case BINDER_SET_MAX_THREADS: {
		int max_threads = 15;
		if (arg) {
			err = verify_area(VERIFY_READ, (void *)arg, sizeof(int));
			if (!err)
				max_threads = get_user((int *)arg);
		}
		proc->max_threads = max_threads;
		return 0;
	}

	case BINDER_SET_CONTEXT_MGR:
		if (current->euid != 0)
			return -EPERM;
		if (binder_context_mgr != NULL && binder_context_mgr != proc)
			return -EBUSY;
		binder_context_mgr = proc;
		proc->is_context_mgr = 1;
		return 0;

	case BINDER_IOC_TRANSACT:
		return binder_do_transact(proc, (struct binder_ipc_msg *)arg);

	case BINDER_IOC_RECV:
		return binder_do_recv(proc, (struct binder_ipc_msg *)arg,
				      (filp->f_flags & O_NONBLOCK) ? 1 : 0);

	case BINDER_IOC_REPLY:
		return binder_do_reply(proc, (struct binder_ipc_msg *)arg);

	case BINDER_IOC_REGISTER_SVC: {
		struct binder_service_info info;
		int h;

		err = verify_area(VERIFY_WRITE, (void *)arg, sizeof(info));
		if (err)
			return err;
		memcpy_fromfs(&info, (void *)arg, sizeof(info));
		h = binder_register_service_internal(
			proc, info.name, info.descriptor,
			current->pid, current->euid);
		if (h < 0)
			return h;
		info.handle = h;
		info.owner_pid = current->pid;
		info.owner_uid = current->euid;
		memcpy_tofs((void *)arg, &info, sizeof(info));
		return 0;
	}

	case BINDER_IOC_LOOKUP_SVC: {
		struct binder_service_info info;
		struct binder_svc_entry *svc;

		err = verify_area(VERIFY_WRITE, (void *)arg, sizeof(info));
		if (err)
			return err;
		memcpy_fromfs(&info, (void *)arg, sizeof(info));
		svc = (info.name[0] != '\0') ?
		      binder_find_svc_by_name(info.name) :
		      binder_find_svc_by_handle(info.handle);
		if (!svc)
			return -ENOENT;
		info.handle = svc->handle;
		info.owner_pid = svc->owner_pid;
		info.owner_uid = svc->owner_uid;
		info.txn_count = svc->txn_count;
		strcpy(info.name, svc->name);
		strcpy(info.descriptor, svc->descriptor);
		memcpy_tofs((void *)arg, &info, sizeof(info));
		return 0;
	}

	case BINDER_IOC_LIST_SVCS: {
		struct binder_service_info info;
		int idx, found = 0, i;

		err = verify_area(VERIFY_WRITE, (void *)arg, sizeof(info));
		if (err)
			return err;
		memcpy_fromfs(&info, (void *)arg, sizeof(info));
		idx = info.handle; /* Caller passes 0-based index in `handle` */
		if (idx < 0)
			return -EINVAL;
		for (i = 0; i < BINDER_MAX_SERVICES; i++) {
			if (!binder_svcs[i].in_use)
				continue;
			if (found == idx) {
				info.handle = binder_svcs[i].handle;
				info.owner_pid = binder_svcs[i].owner_pid;
				info.owner_uid = binder_svcs[i].owner_uid;
				info.txn_count = binder_svcs[i].txn_count;
				strcpy(info.name, binder_svcs[i].name);
				strcpy(info.descriptor, binder_svcs[i].descriptor);
				memcpy_tofs((void *)arg, &info, sizeof(info));
				return 0;
			}
			found++;
		}
		return -ENOENT;
	}

	case BINDER_WRITE_READ: {
		struct binder_write_read bwr;
		err = verify_area(VERIFY_WRITE, (void *)arg, sizeof(bwr));
		if (err)
			return err;
		memcpy_fromfs(&bwr, (void *)arg, sizeof(bwr));
		if (bwr.write_size >= sizeof(struct binder_ipc_msg) && bwr.write_buffer) {
			err = binder_do_transact(proc, (struct binder_ipc_msg *)bwr.write_buffer);
			if (err)
				return err;
			bwr.write_consumed = bwr.write_size;
		}
		if (bwr.read_size >= sizeof(struct binder_ipc_msg) && bwr.read_buffer) {
			err = binder_do_recv(proc, (struct binder_ipc_msg *)bwr.read_buffer, 1);
			if (err == 0)
				bwr.read_consumed = sizeof(struct binder_ipc_msg);
		}
		memcpy_tofs((void *)arg, &bwr, sizeof(bwr));
		return 0;
	}

	case BINDER_IOC_RECV_NONBLOCK:
		return binder_do_recv(proc, (struct binder_ipc_msg *)arg, 1);

	case BINDER_IOC_UEVENT_EMIT: {
		struct binder_uevent_msg kev;
		int i;
		err = verify_area(VERIFY_WRITE, (void *)arg, sizeof(kev));
		if (err)
			return err;
		memcpy_fromfs(&kev, (void *)arg, sizeof(kev));
		if (!kev.subsystem[0])
			strcpy(kev.subsystem, "block");
		if (!kev.devpath[0])
			strcpy(kev.devpath, "/devices/pci0000:00/usb1/1-1/block/sda/sda1");
		if (!kev.devname[0])
			strcpy(kev.devname, "sda1");
		if (kev.major <= 0)
			kev.major = 8;
		if (kev.minor <= 0)
			kev.minor = 1;
		if (!kev.fstype[0])
			strcpy(kev.fstype, "ext2");
		if (!kev.uuid[0])
			strcpy(kev.uuid, "4A8F-9C21");
		if (!kev.label[0])
			strcpy(kev.label, "SAN_DISK_USB");

		if (strcmp(kev.action, "prepare") == 0) {
			usb_sd_set_online(1, kev.label, kev.uuid, kev.fstype);
			kev.online = 1;
			kev.sectors = 4096;
			memcpy_tofs((void *)arg, &kev, sizeof(kev));
			return 0;
		} else if (strcmp(kev.action, "add") == 0) {
			usb_sd_set_online(1, kev.label, kev.uuid, kev.fstype);
			kev.online = 1;
			kev.sectors = 4096;
		} else if (strcmp(kev.action, "remove") == 0) {
			usb_sd_set_online(0, NULL, NULL, NULL);
			kev.online = 0;
			kev.sectors = 0;
		} else {
			usb_sd_get_meta(&kev.online, kev.label, kev.uuid,
					kev.fstype, &kev.sectors);
		}

		kev.seqnum = ++global_uevent_seq;
		sprintf(kev.raw_env,
			"%s@%s\nACTION=%s\nDEVPATH=%s\nSUBSYSTEM=%s\nDEVNAME=%s\n"
			"MAJOR=%d\nMINOR=%d\nID_FS_TYPE=%s\nID_FS_UUID=%s\n"
			"ID_FS_LABEL=%s\nSEQNUM=%u\n",
			kev.action, kev.devpath, kev.action, kev.devpath,
			kev.subsystem, kev.devname, kev.major, kev.minor,
			kev.fstype, kev.uuid, kev.label, kev.seqnum);

		uevent_ring[kev.seqnum % BINDER_UEVENT_RING_SIZE] = kev;
		for (i = 0; i < BINDER_MAX_PROCS; i++) {
			if (binder_procs[i].in_use)
				wake_up_interruptible(&binder_procs[i].wait);
		}
		memcpy_tofs((void *)arg, &kev, sizeof(kev));
		return 0;
	}

	case BINDER_IOC_UEVENT_POLL: {
		struct binder_uevent_msg kev;
		err = verify_area(VERIFY_WRITE, (void *)arg, sizeof(kev));
		if (err)
			return err;
		if (proc->last_uevent_seq >= global_uevent_seq)
			return -EAGAIN;
		proc->last_uevent_seq++;
		if (global_uevent_seq - proc->last_uevent_seq >= BINDER_UEVENT_RING_SIZE)
			proc->last_uevent_seq = global_uevent_seq;
		kev = uevent_ring[proc->last_uevent_seq % BINDER_UEVENT_RING_SIZE];
		memcpy_tofs((void *)arg, &kev, sizeof(kev));
		return 0;
	}

	case BINDER_IOC_USB_STATUS: {
		struct binder_uevent_msg kev;
		err = verify_area(VERIFY_WRITE, (void *)arg, sizeof(kev));
		if (err)
			return err;
		memset(&kev, 0, sizeof(kev));
		usb_sd_get_meta(&kev.online, kev.label, kev.uuid,
				kev.fstype, &kev.sectors);
		strcpy(kev.subsystem, "block");
		strcpy(kev.devpath, "/devices/pci0000:00/usb1/1-1/block/sda/sda1");
		strcpy(kev.devname, "sda1");
		kev.major = 8;
		kev.minor = 1;
		kev.seqnum = global_uevent_seq;
		memcpy_tofs((void *)arg, &kev, sizeof(kev));
		return 0;
	}

	case BINDER_IOC_WAIT_EVENT: {
		struct binder_wait_event wev;
		err = verify_area(VERIFY_WRITE, (void *)arg, sizeof(wev));
		if (err)
			return err;
		while (proc->todo == NULL && proc->last_uevent_seq >= global_uevent_seq) {
			if (current->signal & ~current->blocked)
				return -ERESTARTSYS;
			interruptible_sleep_on(&proc->wait);
		}
		memset(&wev, 0, sizeof(wev));
		if (proc->last_uevent_seq < global_uevent_seq) {
			proc->last_uevent_seq++;
			if (global_uevent_seq - proc->last_uevent_seq >= BINDER_UEVENT_RING_SIZE)
				proc->last_uevent_seq = global_uevent_seq;
			wev.event_type = BINDER_WAIT_UEVENT;
			wev.uevent = uevent_ring[proc->last_uevent_seq % BINDER_UEVENT_RING_SIZE];
			memcpy_tofs((void *)arg, &wev, sizeof(wev));
			return 0;
		}
		if (proc->todo != NULL) {
			struct binder_txn *txn = proc->todo;
			proc->todo = txn->next;
			txn->delivered = 1;
			wev.event_type = BINDER_WAIT_TXN;
			wev.txn = txn->msg;
			if (txn->msg.flags & TF_ONE_WAY) {
				kfree(txn);
			} else {
				txn->next = proc->in_flight;
				proc->in_flight = txn;
			}
			memcpy_tofs((void *)arg, &wev, sizeof(wev));
			return 0;
		}
		return -EAGAIN;
	}

	default:
		return -EINVAL;
	}
}

static struct file_operations binder_fops = {
	NULL,		/* lseek */
	NULL,		/* read */
	NULL,		/* write */
	NULL,		/* readdir */
	binder_select,	/* select */
	binder_ioctl,	/* ioctl */
	NULL,		/* mmap */
	binder_open,	/* open */
	binder_release,	/* release */
	NULL,		/* fsync */
	NULL,		/* fasync */
	NULL,		/* check_media_change */
	NULL		/* revalidate */
};

int get_binder_info(char *buf)
{
	int len = 0, i, svc_count = 0, proc_count = 0;

	len += sprintf(buf + len,
		       "binder state (protocol v%d, /dev/binder %d:%d):\n",
		       BINDER_CURRENT_PROTOCOL_VERSION,
		       BINDER_CHAR_MAJOR, BINDER_CHAR_MINOR);

	if (binder_context_mgr && binder_context_mgr->in_use) {
		len += sprintf(buf + len,
			       "  context_manager: handle=0 pid=%d uid=%d comm=%s\n",
			       binder_context_mgr->pid,
			       binder_context_mgr->uid,
			       binder_context_mgr->comm);
	} else {
		len += sprintf(buf + len, "  context_manager: none\n");
	}

	len += sprintf(buf + len,
		       "  transactions: total=%lu sync=%lu oneway=%lu replies=%lu\n",
		       stat_total_txns, stat_sync_txns,
		       stat_oneway_txns, stat_replies);

	len += sprintf(buf + len, "  services:\n");
	for (i = 0; i < BINDER_MAX_SERVICES && len < 3400; i++) {
		if (!binder_svcs[i].in_use)
			continue;
		svc_count++;
		len += sprintf(buf + len,
			       "    handle %-2d : %-16s [%s] (pid=%d uid=%d txns=%u)\n",
			       binder_svcs[i].handle,
			       binder_svcs[i].name,
			       binder_svcs[i].descriptor,
			       binder_svcs[i].owner_pid,
			       binder_svcs[i].owner_uid,
			       binder_svcs[i].txn_count);
	}
	if (svc_count == 0)
		len += sprintf(buf + len, "    (no services registered)\n");

	len += sprintf(buf + len, "  procs:\n");
	for (i = 0; i < BINDER_MAX_PROCS && len < 3800; i++) {
		if (!binder_procs[i].in_use)
			continue;
		proc_count++;
		len += sprintf(buf + len,
			       "    proc pid=%-4d uid=%-4d comm=%-14s sent=%u rcvd=%u%s\n",
			       binder_procs[i].pid,
			       binder_procs[i].uid,
			       binder_procs[i].comm,
			       binder_procs[i].txns_sent,
			       binder_procs[i].txns_received,
			       binder_procs[i].is_context_mgr ? " [CONTEXT_MGR]" : "");
	}
	if (proc_count == 0)
		len += sprintf(buf + len, "    (no active binder procs)\n");

	return len;
}

int binder_init(void)
{
	if (register_chrdev(BINDER_CHAR_MAJOR, "binder", &binder_fops)) {
		printk("binder: unable to register char major %d\n", BINDER_CHAR_MAJOR);
		return -EIO;
	}
	printk("binder: Android Binder IPC initialized (/dev/binder %d:%d, protocol v%d)\n",
	       BINDER_CHAR_MAJOR, BINDER_CHAR_MINOR, BINDER_CURRENT_PROTOCOL_VERSION);
	return 0;
}
