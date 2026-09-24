/*
 * applications/vold/vold.c
 *
 * Android Volume Daemon (vold) for SIX.
 *
 * Responsibilities:
 *   1. Registers "vold" ([android.os.IVold]) with servicemanager over /dev/binder.
 *   2. Listens for kernel NETLINK_KOBJECT_UEVENT block hotplug messages
 *      (ACTION=add / ACTION=remove for /dev/sda1, major 8 minor 1).
 *   3. Notifies StorageManagerService ("mount" [android.os.storage.IStorageManager])
 *      via oneway IVoldListener Binder IPC callbacks (onDiskCreated,
 *      onVolumeCreated, onVolumeStateChanged, onDiskDestroyed).
 *   4. Executes privileged storage operations requested by StorageManagerService
 *      over Binder:
 *        - IVold::mount / IVold::unmount (/mnt/media_rw/usb or /mnt/expand/usb)
 *        - IVold::partition(diskId, PUBLIC)  -> ext2 portable USB storage
 *        - IVold::partition(diskId, PRIVATE) -> dm-crypt ChaCha20-256 FDE
 *          Adoptable Storage on /dev/mapper/crypt_usb (/dev/dm-2)
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/binder.h>
#include <linux/dm.h>

extern int errno;

/* IVold Binder transaction codes */
#define IVOLD_GET_STATUS		1
#define IVOLD_MOUNT			2
#define IVOLD_UNMOUNT			3
#define IVOLD_PARTITION			4

/* IVoldListener oneway Binder callback codes sent to StorageManagerService ("mount") */
#define IVOLD_LISTENER_ON_DISK_CREATED		101
#define IVOLD_LISTENER_ON_VOLUME_CREATED	102
#define IVOLD_LISTENER_ON_VOLUME_STATE_CHANGED	103
#define IVOLD_LISTENER_ON_DISK_DESTROYED	104

struct vold_state {
	int disk_present;
	char disk_id[24];		/* "disk:8,0" */
	char vol_id[24];		/* "public:8,1" or "private:8,1" */
	char vol_type[16];		/* "PUBLIC" or "PRIVATE" */
	char dev_node[32];		/* "/dev/sda1" or "/dev/mapper/crypt_usb" */
	char mount_path[48];		/* "/mnt/media_rw/usb" or "/mnt/expand/usb" */
	char state[16];			/* "UNMOUNTED", "MOUNTED", "REMOVED" */
	char label[32];
	char uuid[32];
	char fstype[16];
	int encrypted;
	char key_hex[68];
	unsigned int uevents_handled;
};

static struct vold_state vstate;

static int run_helper(char *prog, char *arg1)
{
	int pid, status = 0;
	char *argv[3];

	argv[0] = prog;
	argv[1] = arg1;
	argv[2] = 0;

	pid = fork();
	if (pid == 0) {
		int null_fd = open("/dev/null", 2);
		if (null_fd >= 0) {
			dup2(null_fd, 1);
			dup2(null_fd, 2);
			if (null_fd > 2)
				close(null_fd);
		}
		execve(prog, argv, 0);
		_exit(127);
	}
	if (pid > 0) {
		waitpid(pid, &status, 0);
		return status;
	}
	return -1;
}

static int run_helper2(char *prog, char *arg1, char *arg2)
{
	int pid, status = 0;
	char *argv[4];

	argv[0] = prog;
	argv[1] = arg1;
	argv[2] = arg2;
	argv[3] = 0;

	pid = fork();
	if (pid == 0) {
		int null_fd = open("/dev/null", 2);
		if (null_fd >= 0) {
			dup2(null_fd, 1);
			dup2(null_fd, 2);
			if (null_fd > 2)
				close(null_fd);
		}
		execve(prog, argv, 0);
		_exit(127);
	}
	if (pid > 0) {
		waitpid(pid, &status, 0);
		return status;
	}
	return -1;
}

static const char *vold_probe_fs(const char *dev)
{
	unsigned char buf[2048];
	int fd;

	fd = open(dev, 0);
	if (fd < 0)
		return NULL;
	memset(buf, 0, sizeof(buf));
	read(fd, buf, sizeof(buf));
	close(fd);

	/* NTFS Boot Sector OEM ID at offset 0x03: "NTFS    " */
	if (memcmp(buf + 3, "NTFS    ", 8) == 0)
		return "ntfs";
	/* ext2/ext4 superblock magic 0xEF53 at offset 1080 (0x438) */
	if (buf[1080] == 0x53 && buf[1081] == 0xef)
		return "ext2";
	return NULL;
}

static int setup_dm_crypt_usb(const char *key_hex)
{
	int fd;
	struct dm_ioctl_req req;

	fd = open("/dev/mapper/control", 2);
	if (fd < 0)
		fd = open("/dev/dm-0", 0);
	if (fd < 0)
		return -1;

	/* Remove any existing crypt_usb device by name first */
	memset(&req, 0, sizeof(req));
	req.minor = -1;
	strcpy(req.name, "crypt_usb");
	ioctl(fd, DM_IOC_REMOVE, &req);

	/* Create crypt_usb on next available minor with dm-crypt over /dev/sda1 (8:1) */
	memset(&req, 0, sizeof(req));
	req.minor = -1;
	strcpy(req.name, "crypt_usb");
	req.ro = 0;
	req.num_targets = 1;
	req.targets[0].start_sector = 0;
	req.targets[0].num_sectors = 4096;
	req.targets[0].type = DM_TARGET_CRYPT;
	req.targets[0].bdev = (8 << 8) | 1;
	strcpy(req.targets[0].dev_name, "/dev/sda1");
	req.targets[0].offset_sector = 0;
	strcpy(req.targets[0].cipher, "chacha20");
	strncpy(req.targets[0].key, key_hex, DM_KEY_LEN - 1);
	req.targets[0].key[DM_KEY_LEN - 1] = '\0';
	req.targets[0].iv_offset = 0;

	if (ioctl(fd, DM_IOC_CREATE, &req) < 0) {
		close(fd);
		return -1;
	}
	close(fd);

	unlink("/dev/mapper/crypt_usb");
	mknod("/dev/mapper/crypt_usb", 060660, (DM_MAJOR << 8) | req.minor);
	return 0;
}

static void teardown_dm_crypt_usb(void)
{
	int fd;
	struct dm_ioctl_req req;

	fd = open("/dev/mapper/control", 2);
	if (fd < 0)
		fd = open("/dev/dm-0", 0);
	if (fd >= 0) {
		memset(&req, 0, sizeof(req));
		req.minor = -1;
		strcpy(req.name, "crypt_usb");
		ioctl(fd, DM_IOC_REMOVE, &req);
		close(fd);
	}
	unlink("/dev/mapper/crypt_usb");
}

static void notify_storage_manager(int bfd, unsigned int code, const char *payload)
{
	struct binder_service_info sinfo;
	struct binder_ipc_msg msg;

	memset(&sinfo, 0, sizeof(sinfo));
	strcpy(sinfo.name, "mount");
	if (ioctl(bfd, BINDER_IOC_LOOKUP_SVC, &sinfo) < 0 || sinfo.handle <= 0)
		return;

	memset(&msg, 0, sizeof(msg));
	msg.target_handle = sinfo.handle;
	msg.code = code;
	msg.flags = TF_ONE_WAY;
	strcpy(msg.interface_token, "android.os.IVoldListener");
	if (payload) {
		strncpy(msg.data, payload, BINDER_MAX_DATA_SIZE - 1);
		msg.data_size = strlen(msg.data) + 1;
	}
	ioctl(bfd, BINDER_IOC_TRANSACT, &msg);
}

static int vold_do_mount(int bfd)
{
	const char *probed;

	if (!vstate.disk_present)
		return -1;
	if (strcmp(vstate.state, "MOUNTED") == 0)
		return 0;

	mkdir("/mnt", 0755);
	mkdir("/mnt/media_rw", 0755);
	mkdir("/mnt/media_rw/usb", 0755);
	mkdir("/mnt/expand", 0755);
	mkdir("/mnt/expand/usb", 0755);

	umount(vstate.mount_path);

	probed = vold_probe_fs(vstate.dev_node);
	if ((probed && strcmp(probed, "ntfs") == 0) ||
	    strcmp(vstate.fstype, "ntfs") == 0) {
		strcpy(vstate.fstype, "ntfs");
		strcpy(vstate.vol_type, "PUBLIC(NTFS)");
		if (run_helper2("/bin/ntfs-3g", vstate.dev_node, vstate.mount_path) != 0)
			return -1;
	} else {
		if (mount(vstate.dev_node, vstate.mount_path, "ext2", 0, 0) < 0)
			return -1;
	}

	strcpy(vstate.state, "MOUNTED");
	{
		char payload[256];
		sprintf(payload, "%s|%s|%s|%s|%s|%s",
			vstate.vol_id, vstate.vol_type, vstate.state,
			vstate.mount_path, vstate.label, vstate.uuid);
		notify_storage_manager(bfd, IVOLD_LISTENER_ON_VOLUME_STATE_CHANGED, payload);
	}
	return 0;
}

static int vold_do_unmount(int bfd)
{
	if (!vstate.disk_present)
		return -1;
	if (strcmp(vstate.state, "MOUNTED") == 0) {
		sync();
		umount(vstate.mount_path);
		strcpy(vstate.state, "UNMOUNTED");
		{
			char payload[256];
			sprintf(payload, "%s|%s|%s|%s|%s|%s",
				vstate.vol_id, vstate.vol_type, vstate.state,
				"none", vstate.label, vstate.uuid);
			notify_storage_manager(bfd, IVOLD_LISTENER_ON_VOLUME_STATE_CHANGED, payload);
		}
	}
	return 0;
}

static int vold_do_partition(int bfd, const char *mode)
{
	if (!vstate.disk_present)
		return -1;

	vold_do_unmount(bfd);
	teardown_dm_crypt_usb();

	if (mode && strcmp(mode, "private") == 0) {
		int kfd;
		strcpy(vstate.key_hex,
		       "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08");
		mkdir("/data", 0755);
		mkdir("/data/misc", 0700);
		mkdir("/data/misc/vold", 0700);
		kfd = open("/data/misc/vold/expand_usb.key", 0100 | 01000 | 2, 0600);
		if (kfd >= 0) {
			write(kfd, vstate.key_hex, 64);
			write(kfd, "\n", 1);
			close(kfd);
		}
		if (setup_dm_crypt_usb(vstate.key_hex) < 0)
			return -1;
		run_helper("/bin/mkfs.ext2", "/dev/mapper/crypt_usb");
		strcpy(vstate.vol_id, "private:8,1");
		strcpy(vstate.vol_type, "PRIVATE");
		strcpy(vstate.dev_node, "/dev/mapper/crypt_usb");
		strcpy(vstate.mount_path, "/mnt/expand/usb");
		strcpy(vstate.label, "ADOPTABLE_USB");
		strcpy(vstate.uuid, "CRYPT-8A01");
		strcpy(vstate.fstype, "dm-crypt+ext2");
		vstate.encrypted = 1;
		if (vold_do_mount(bfd) == 0) {
			int fd = open("/mnt/expand/usb/ADOPTABLE_KEY_INFO.txt",
				      0100 | 01000 | 2, 0644);
			if (fd >= 0) {
				const char *msg =
					"Android Adoptable Storage (vold dm-crypt ChaCha20-256)\n"
					"Backing Device: /dev/sda1 (8:1)\n"
					"Mapped Device:  /dev/mapper/crypt_usb\n"
					"Key Location:   /data/misc/vold/expand_usb.key\n";
				write(fd, msg, strlen(msg));
				close(fd);
			}
			sync();
		}
		return 0;
	} else if (mode && strcmp(mode, "ntfs") == 0) {
		struct binder_uevent_msg uev;
		memset(&uev, 0, sizeof(uev));
		strcpy(uev.action, "prepare");
		strcpy(uev.subsystem, "block");
		strcpy(uev.devpath, "/devices/pci0000:00/usb1/1-1/block/sda/sda1");
		strcpy(uev.devname, "sda1");
		uev.major = 8;
		uev.minor = 1;
		strcpy(uev.fstype, "ntfs");
		strcpy(uev.label, "SANDISK_NTFS");
		strcpy(uev.uuid, "6A1B-8E42");
		ioctl(bfd, BINDER_IOC_UEVENT_EMIT, &uev);

		strcpy(vstate.vol_id, "public:8,1");
		strcpy(vstate.vol_type, "PUBLIC(NTFS)");
		strcpy(vstate.dev_node, "/dev/sda1");
		strcpy(vstate.mount_path, "/mnt/media_rw/usb");
		strcpy(vstate.label, "SANDISK_NTFS");
		strcpy(vstate.uuid, "6A1B-8E42");
		strcpy(vstate.fstype, "ntfs");
		vstate.encrypted = 0;
		return vold_do_mount(bfd);
	} else {
		run_helper("/bin/mkfs.ext2", "/dev/sda1");
		strcpy(vstate.vol_id, "public:8,1");
		strcpy(vstate.vol_type, "PUBLIC");
		strcpy(vstate.dev_node, "/dev/sda1");
		strcpy(vstate.mount_path, "/mnt/media_rw/usb");
		strcpy(vstate.label, "SAN_DISK_USB");
		strcpy(vstate.uuid, "4A8F-9C21");
		strcpy(vstate.fstype, "ext2");
		vstate.encrypted = 0;
		if (vold_do_mount(bfd) == 0) {
			int fd = open("/mnt/media_rw/usb/README_USB.txt",
				      0100 | 01000 | 2, 0644);
			if (fd >= 0) {
				const char *msg =
					"SanDisk Ultra USB 3.0 Flash Drive (2048 KB)\n"
					"Auto-mounted by Android vold + StorageManagerService over /dev/binder!\n";
				write(fd, msg, strlen(msg));
				close(fd);
			}
			sync();
		}
		return 0;
	}
}

static void handle_uevent(int bfd, struct binder_uevent_msg *uev)
{
	vstate.uevents_handled++;

	if (strcmp(uev->action, "add") == 0) {
		char payload[256];

		vstate.disk_present = 1;
		strcpy(vstate.disk_id, "disk:8,0");
		strcpy(vstate.vol_id, "public:8,1");
		strcpy(vstate.vol_type, "PUBLIC");
		strcpy(vstate.dev_node, "/dev/sda1");
		strcpy(vstate.mount_path, "/mnt/media_rw/usb");
		strcpy(vstate.state, "UNMOUNTED");
		strncpy(vstate.label, uev->label[0] ? uev->label : "SAN_DISK_USB", 31);
		strncpy(vstate.uuid, uev->uuid[0] ? uev->uuid : "4A8F-9C21", 31);
		strncpy(vstate.fstype, uev->fstype[0] ? uev->fstype : "ext2", 15);
		vstate.encrypted = 0;

		sprintf(payload, "%s|SanDisk Ultra USB 3.0|USB|%lu",
			vstate.disk_id, uev->sectors ? uev->sectors : 1024UL);
		notify_storage_manager(bfd, IVOLD_LISTENER_ON_DISK_CREATED, payload);

		sprintf(payload, "%s|%s|%s|%s|%s|%s",
			vstate.vol_id, vstate.vol_type, vstate.state,
			vstate.mount_path, vstate.label, vstate.uuid);
		notify_storage_manager(bfd, IVOLD_LISTENER_ON_VOLUME_CREATED, payload);
	} else if (strcmp(uev->action, "remove") == 0) {
		if (strcmp(vstate.state, "MOUNTED") == 0) {
			umount(vstate.mount_path);
		}
		if (vstate.encrypted) {
			teardown_dm_crypt_usb();
		}
		vstate.disk_present = 0;
		strcpy(vstate.state, "REMOVED");
		notify_storage_manager(bfd, IVOLD_LISTENER_ON_DISK_DESTROYED, "disk:8,0");
	}
}

static void handle_binder_txn(int bfd, struct binder_ipc_msg *msg)
{
	struct binder_ipc_msg reply;

	memset(&reply, 0, sizeof(reply));
	reply.txn_id = msg->txn_id;
	reply.status = 0;

	switch (msg->code) {
	case PING_TRANSACTION:
		sprintf(reply.data, "PONG from vold (pid=%d, uevents=%u)",
			getpid(), vstate.uevents_handled);
		reply.data_size = strlen(reply.data) + 1;
		break;

	case IVOLD_GET_STATUS:
		if (!vstate.disk_present) {
			sprintf(reply.data,
				"vold status: ONLINE (pid=%d, uevents=%u)\n"
				"  disks: none (unplugged)",
				getpid(), vstate.uevents_handled);
		} else {
			sprintf(reply.data,
				"vold status: ONLINE (pid=%d, uevents=%u)\n"
				"  disk %-10s : SanDisk Ultra USB 3.0 (/dev/sda 8:0, 512 KB, flags=USB)\n"
				"  vol  %-10s : type=%-7s state=%-9s dev=%-21s mount=%s (label=%s uuid=%s)",
				getpid(), vstate.uevents_handled,
				vstate.disk_id, vstate.vol_id, vstate.vol_type,
				vstate.state, vstate.dev_node,
				strcmp(vstate.state, "MOUNTED") == 0 ? vstate.mount_path : "none",
				vstate.label, vstate.uuid);
		}
		reply.data_size = strlen(reply.data) + 1;
		break;

	case IVOLD_MOUNT:
		if (vold_do_mount(bfd) == 0) {
			sprintf(reply.data, "vold: mounted %s (%s) at %s",
				vstate.vol_id, vstate.dev_node, vstate.mount_path);
		} else {
			reply.status = -1;
			sprintf(reply.data, "vold: failed to mount %s", vstate.vol_id);
		}
		reply.data_size = strlen(reply.data) + 1;
		break;

	case IVOLD_UNMOUNT:
		if (vold_do_unmount(bfd) == 0) {
			sprintf(reply.data, "vold: unmounted %s", vstate.vol_id);
		} else {
			reply.status = -1;
			sprintf(reply.data, "vold: failed to unmount %s", vstate.vol_id);
		}
		reply.data_size = strlen(reply.data) + 1;
		break;

	case IVOLD_PARTITION:
		if (vold_do_partition(bfd, msg->data[0] ? msg->data : "public") == 0) {
			sprintf(reply.data,
				"vold: partitioned %s as %s -> %s mounted at %s (%s)",
				vstate.disk_id, vstate.vol_type, vstate.dev_node,
				vstate.mount_path, vstate.fstype);
		} else {
			reply.status = -1;
			sprintf(reply.data, "vold: partition failed on %s", vstate.disk_id);
		}
		reply.data_size = strlen(reply.data) + 1;
		break;

	default:
		sprintf(reply.data, "IVold[code=%u]: handled by vold (pid=%d)",
			msg->code, getpid());
		reply.data_size = strlen(reply.data) + 1;
		break;
	}

	if (!(msg->flags & TF_ONE_WAY))
		ioctl(bfd, BINDER_IOC_REPLY, &reply);
}

int main(int argc, char **argv)
{
	int bfd;
	struct binder_service_info svc;
	struct binder_wait_event wev;

	memset(&vstate, 0, sizeof(vstate));
	strcpy(vstate.state, "REMOVED");

	mkdir("/mnt", 0755);
	mkdir("/mnt/media_rw", 0755);
	mkdir("/mnt/media_rw/usb", 0755);
	mkdir("/mnt/expand", 0755);
	mkdir("/mnt/expand/usb", 0755);

	bfd = open("/dev/binder", 2);
	if (bfd < 0) {
		printf("vold: cannot open /dev/binder (errno=%d)\n", errno);
		return 1;
	}

	memset(&svc, 0, sizeof(svc));
	strcpy(svc.name, "vold");
	strcpy(svc.descriptor, "android.os.IVold");
	if (ioctl(bfd, BINDER_IOC_REGISTER_SVC, &svc) < 0) {
		printf("vold: failed to register android.os.IVold\n");
		close(bfd);
		return 1;
	}

	printf("vold: Android Volume Daemon 2.0 started (pid=%d, handle=%d [android.os.IVold])\n",
	       getpid(), svc.handle);

	for (;;) {
		if (ioctl(bfd, BINDER_IOC_WAIT_EVENT, &wev) < 0)
			continue;
		if (wev.event_type == BINDER_WAIT_UEVENT) {
			handle_uevent(bfd, &wev.uevent);
		} else if (wev.event_type == BINDER_WAIT_TXN) {
			handle_binder_txn(bfd, &wev.txn);
		}
	}

	close(bfd);
	return 0;
}
