/*
 * applications/usbctl/usbctl.c
 *
 * Simulated USB Mass Storage Hotplug Controller (/bin/usbctl) for SIX.
 *
 * Usage:
 *   usbctl status
 *   usbctl plug [label] [uuid]
 *   usbctl unplug
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/binder.h>

extern int errno;

static int run_mkfs(const char *dev)
{
	int pid, status = 0;
	char *argv[3];

	argv[0] = "/bin/mkfs.ext2";
	argv[1] = (char *)dev;
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
		execve("/bin/mkfs.ext2", argv, 0);
		_exit(127);
	}
	if (pid > 0) {
		waitpid(pid, &status, 0);
		return status;
	}
	return -1;
}

static void populate_usb_filesystem(const char *label, const char *uuid)
{
	int fd;
	char buf[256];

	mkdir("/tmp", 0777);
	mkdir("/tmp/.usb_stage", 0755);
	if (mount("/dev/sda1", "/tmp/.usb_stage", "ext2", 0, 0) == 0) {
		mkdir("/tmp/.usb_stage/DCIM", 0755);
		fd = open("/tmp/.usb_stage/README_USB.txt", 0100 | 01000 | 2, 0644);
		if (fd >= 0) {
			sprintf(buf,
				"=== SanDisk Ultra USB 3.0 Flash Drive ===\n"
				"Label:      %s\n"
				"UUID:       %s\n"
				"Device:     /dev/sda1 (8:1, 512 KB ext2)\n"
				"Hotplugged: via NETLINK_KOBJECT_UEVENT -> vold -> Binder -> StorageManagerService\n",
				label, uuid);
			write(fd, buf, strlen(buf));
			close(fd);
		}
		fd = open("/tmp/.usb_stage/DCIM/IMG_0001.TXT", 0100 | 01000 | 2, 0644);
		if (fd >= 0) {
			const char *img = "Camera DCIM sample photo metadata (SanDisk USB)\n";
			write(fd, img, strlen(img));
			close(fd);
		}
		sync();
		umount("/tmp/.usb_stage");
	}
	rmdir("/tmp/.usb_stage");
}

static void usage(void)
{
	printf("Usage:\n");
	printf("  usbctl status                       Show simulated USB controller & drive state\n");
	printf("  usbctl plug [ext2|ntfs] [LABEL]     Plug in simulated USB drive & emit NETLINK_KOBJECT_UEVENT\n");
	printf("  usbctl unplug                       Unplug simulated USB drive & emit NETLINK_KOBJECT_UEVENT\n");
}

int main(int argc, char **argv)
{
	int bfd;
	struct binder_uevent_msg uev;

	if (argc < 2) {
		usage();
		return 1;
	}

	bfd = open("/dev/binder", 2);
	if (bfd < 0) {
		printf("usbctl: cannot open /dev/binder (errno=%d)\n", errno);
		return 1;
	}

	if (strcmp(argv[1], "status") == 0) {
		if (ioctl(bfd, BINDER_IOC_USB_STATUS, &uev) < 0) {
			printf("usbctl: BINDER_IOC_USB_STATUS failed\n");
			close(bfd);
			return 1;
		}
		printf("USB Mass Storage Controller (six_xhci usb1/1-1):\n");
		printf("  State:       %s\n", uev.online ? "CONNECTED (/dev/sda 8:0, /dev/sda1 8:1)" : "DISCONNECTED");
		if (uev.online) {
			printf("  Capacity:    %lu sectors (%lu KB)\n", uev.sectors, uev.sectors >> 1);
			printf("  Label:       %s\n", uev.label);
			printf("  UUID:        %s\n", uev.uuid);
			printf("  Filesystem:  %s\n", uev.fstype);
		}
		printf("  Uevent Seq:  %u\n", uev.seqnum);
		close(bfd);
		return 0;
	}

	if (strcmp(argv[1], "plug") == 0) {
		int is_ntfs = 0;
		const char *label = "SAN_DISK_USB";
		const char *uuid  = "4A8F-9C21";

		if (argc >= 3 && (strcmp(argv[2], "ntfs") == 0 || strcmp(argv[2], "--ntfs") == 0)) {
			is_ntfs = 1;
			label = (argc >= 4) ? argv[3] : "SANDISK_NTFS";
			uuid  = (argc >= 5) ? argv[4] : "6A1B-8E42";
		} else if (argc >= 3 && (strcmp(argv[2], "ext2") == 0 || strcmp(argv[2], "--ext2") == 0)) {
			label = (argc >= 4) ? argv[3] : "SAN_DISK_USB";
			uuid  = (argc >= 5) ? argv[4] : "4A8F-9C21";
		} else {
			if (argc >= 3) label = argv[2];
			if (argc >= 4) uuid  = argv[3];
		}

		/* Step 1: Bring /dev/sda1 online so we can format & populate it */
		memset(&uev, 0, sizeof(uev));
		strcpy(uev.action, "prepare");
		strcpy(uev.subsystem, "block");
		strcpy(uev.devpath, "/devices/pci0000:00/usb1/1-1/block/sda/sda1");
		strcpy(uev.devname, "sda1");
		uev.major = 8;
		uev.minor = 1;
		strcpy(uev.fstype, is_ntfs ? "ntfs" : "ext2");
		strncpy(uev.label, label, sizeof(uev.label) - 1);
		strncpy(uev.uuid, uuid, sizeof(uev.uuid) - 1);
		ioctl(bfd, BINDER_IOC_UEVENT_EMIT, &uev);

		if (!is_ntfs) {
			run_mkfs("/dev/sda1");
			populate_usb_filesystem(label, uuid);
		}

		/* Step 2: Emit the actual kernel NETLINK_KOBJECT_UEVENT (ACTION=add) */
		strcpy(uev.action, "add");
		if (ioctl(bfd, BINDER_IOC_UEVENT_EMIT, &uev) < 0) {
			printf("usbctl: failed to emit uevent\n");
			close(bfd);
			return 1;
		}

		printf("=== Kernel NETLINK_KOBJECT_UEVENT Emitted (SEQNUM=%u) ===\n%s",
		       uev.seqnum, uev.raw_env);
		close(bfd);
		return 0;
	}

	if (strcmp(argv[1], "unplug") == 0) {
		/* Unmount first so buffers flush cleanly before physical detach */
		umount("/mnt/media_rw/usb");
		umount("/mnt/expand/usb");

		memset(&uev, 0, sizeof(uev));
		strcpy(uev.action, "remove");
		strcpy(uev.subsystem, "block");
		strcpy(uev.devpath, "/devices/pci0000:00/usb1/1-1/block/sda/sda1");
		strcpy(uev.devname, "sda1");
		uev.major = 8;
		uev.minor = 1;
		if (ioctl(bfd, BINDER_IOC_UEVENT_EMIT, &uev) < 0) {
			printf("usbctl: failed to emit remove uevent\n");
			close(bfd);
			return 1;
		}
		printf("=== Kernel NETLINK_KOBJECT_UEVENT Emitted (SEQNUM=%u) ===\n%s",
		       uev.seqnum, uev.raw_env);
		close(bfd);
		return 0;
	}

	usage();
	close(bfd);
	return 1;
}
