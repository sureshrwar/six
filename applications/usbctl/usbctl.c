/*
 * applications/usbctl/usbctl.c
 *
 * Simulated USB Mass Storage Hotplug Controller (/bin/usbctl) for SIX.
 *
 * Usage:
 *   usbctl status
 *   usbctl plug [ext2|ext4|ntfs] [LABEL] [UUID]
 *   usbctl ext2|ext4|ntfs [LABEL] [UUID]
 *   usbctl unplug
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/binder.h>

extern int errno;

static void usage(void)
{
	printf("Usage:\n");
	printf("  usbctl status                            Show simulated USB controller & drive state\n");
	printf("  usbctl plug [ext2|ext4|ntfs] [LABEL]     Plug in simulated USB drive (disk/x86/usb_<fs>.img)\n");
	printf("  usbctl ext2|ext4|ntfs [LABEL]            Shorthand for 'usbctl plug <fs>'\n");
	printf("  usbctl unplug                            Unplug simulated USB drive & emit remove uevent\n");
}

int main(int argc, char **argv)
{
	int bfd;
	struct binder_uevent_msg uev;
	const char *cmd;
	int arg_offset = 2;

	if (argc < 2) {
		usage();
		return 1;
	}

	cmd = argv[1];
	/* Support `usbctl ext2`, `usbctl ext4`, `usbctl ntfs` as shorthands for `usbctl plug <fs>` */
	if (strcmp(cmd, "ext2") == 0 || strcmp(cmd, "ext4") == 0 || strcmp(cmd, "ntfs") == 0 ||
	    strcmp(cmd, "--ext2") == 0 || strcmp(cmd, "--ext4") == 0 || strcmp(cmd, "--ntfs") == 0) {
		cmd = "plug";
		arg_offset = 1;
	}

	bfd = open("/dev/binder", 2);
	if (bfd < 0) {
		printf("usbctl: cannot open /dev/binder (errno=%d)\n", errno);
		return 1;
	}

	if (strcmp(cmd, "status") == 0) {
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
			printf("  Filesystem:  %s (disk/x86/usb_%s.img)\n", uev.fstype, uev.fstype);
		}
		printf("  Uevent Seq:  %u\n", uev.seqnum);
		close(bfd);
		return 0;
	}

	if (strcmp(cmd, "plug") == 0) {
		const char *fstype = "ext2";
		const char *label  = "SAN_DISK_USB";
		const char *uuid   = "4A8F-9C21";

		if (argc > arg_offset) {
			const char *a = argv[arg_offset];
			if (strcmp(a, "ntfs") == 0 || strcmp(a, "--ntfs") == 0) {
				fstype = "ntfs";
				label  = (argc > arg_offset + 1) ? argv[arg_offset + 1] : "SANDISK_NTFS";
				uuid   = (argc > arg_offset + 2) ? argv[arg_offset + 2] : "6A1B-8E42";
			} else if (strcmp(a, "ext4") == 0 || strcmp(a, "--ext4") == 0) {
				fstype = "ext4";
				label  = (argc > arg_offset + 1) ? argv[arg_offset + 1] : "SANDISK_EXT4";
				uuid   = (argc > arg_offset + 2) ? argv[arg_offset + 2] : "5B9E-7D31";
			} else if (strcmp(a, "ext2") == 0 || strcmp(a, "--ext2") == 0) {
				fstype = "ext2";
				label  = (argc > arg_offset + 1) ? argv[arg_offset + 1] : "SAN_DISK_USB";
				uuid   = (argc > arg_offset + 2) ? argv[arg_offset + 2] : "4A8F-9C21";
			} else {
				label  = a;
				if (argc > arg_offset + 1)
					uuid = argv[arg_offset + 1];
			}
		}

		/* Ensure any previous mount is cleanly unmounted before hotplugging */
		umount("/dev/block/vold/public:8,1");
		umount("/dev/block/vold/public:8_1");
		umount("/dev/sda1");
		umount("/mnt/media_rw/4A8F-9C21");
		umount("/mnt/media_rw/5B9E-7D31");
		umount("/mnt/media_rw/6A1B-8E42");
		umount("/mnt/media_rw/usb");
		umount("/mnt/expand/CRYPT-8A01");
		umount("/mnt/expand/usb");

		/* Single atomic kernel operation: attach disk/x86/usb_<fs>.img and emit ACTION=add uevent */
		memset(&uev, 0, sizeof(uev));
		strcpy(uev.action, "add");
		strcpy(uev.subsystem, "block");
		strcpy(uev.devpath, "/devices/pci0000:00/usb1/1-1/block/sda/sda1");
		strcpy(uev.devname, "sda1");
		uev.major = 8;
		uev.minor = 1;
		strcpy(uev.fstype, fstype);
		strncpy(uev.label, label, sizeof(uev.label) - 1);
		strncpy(uev.uuid, uuid, sizeof(uev.uuid) - 1);

		if (ioctl(bfd, BINDER_IOC_UEVENT_EMIT, &uev) < 0) {
			printf("usbctl: failed to emit uevent\n");
			close(bfd);
			return 1;
		}

		printf("=== Kernel NETLINK_KOBJECT_UEVENT Emitted (SEQNUM=%u) ===\n%s",
		       uev.seqnum, uev.raw_env);
		usleep(200000);
		close(bfd);
		return 0;
	}

	if (strcmp(cmd, "unplug") == 0) {
		/* Unmount first so buffers flush cleanly before physical detach */
		umount("/dev/block/vold/public:8,1");
		umount("/dev/block/vold/public:8_1");
		umount("/dev/sda1");
		umount("/mnt/media_rw/4A8F-9C21");
		umount("/mnt/media_rw/5B9E-7D31");
		umount("/mnt/media_rw/6A1B-8E42");
		umount("/mnt/media_rw/usb");
		umount("/mnt/expand/CRYPT-8A01");
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
		usleep(200000);
		close(bfd);
		return 0;
	}

	usage();
	close(bfd);
	return 1;
}
