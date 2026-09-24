/*
 *  linux/drivers/block/usb_sd.c
 *
 *  Simulated Hotpluggable USB Mass Storage SCSI Disk driver (/dev/sda, /dev/sda1)
 *  for SIX (major 8, minors 0..15).
 *
 *  Backed directly by external host image files:
 *    - ./disk/x86/usb_ext2.img  (usbctl plug ext2)
 *    - ./disk/x86/usb_ext4.img  (usbctl plug ext4)
 *    - ./disk/x86/usb_ntfs.img  (usbctl plug ntfs)
 *
 *  Works with Kernel NETLINK_KOBJECT_UEVENT (via /dev/binder), /bin/usbctl,
 *  /bin/vold, and /bin/storaged (StorageManagerService).
 */

#define MAJOR_NR SCSI_DISK_MAJOR

#include <solaris.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/blk.h>
#include <asm/segment.h>
#include <asm/system.h>

#define USB_SD_SECTORS		4096UL		/* 2048 KB (4096 x 512B sectors) */
#define USB_SD_MAX_MINORS	16

static int usb_sd_fd = -1;
static int usb_sd_online = 0;
static char usb_sd_label[32] = "SAN_DISK_USB";
static char usb_sd_uuid[32] = "4A8F-9C21";
static char usb_sd_fstype[16] = "ext2";
static char usb_sd_img_path[64] = "./disk/x86/usb_ext2.img";

static int sd_sizes[USB_SD_MAX_MINORS];
static int sd_blocksizes[USB_SD_MAX_MINORS];

int usb_sd_is_online(void)
{
	return usb_sd_online && (usb_sd_fd >= 0);
}

unsigned long usb_sd_get_sectors(void)
{
	return usb_sd_is_online() ? USB_SD_SECTORS : 0UL;
}

void usb_sd_get_meta(int *online, char *label, char *uuid, char *fstype,
		     unsigned long *sectors)
{
	if (online)
		*online = usb_sd_is_online();
	if (label) {
		strncpy(label, usb_sd_label, 31);
		label[31] = '\0';
	}
	if (uuid) {
		strncpy(uuid, usb_sd_uuid, 31);
		uuid[31] = '\0';
	}
	if (fstype) {
		strncpy(fstype, usb_sd_fstype, 15);
		fstype[15] = '\0';
	}
	if (sectors)
		*sectors = usb_sd_is_online() ? USB_SD_SECTORS : 0UL;
}

void usb_sd_set_online(int online, const char *label, const char *uuid,
		       const char *fstype)
{
	extern void force_umount_dev(kdev_t dev);
	int was_online = usb_sd_online && (usb_sd_fd >= 0);
	char prev_path[64];

	strncpy(prev_path, usb_sd_img_path, sizeof(prev_path) - 1);
	prev_path[sizeof(prev_path) - 1] = '\0';

	if (was_online) {
		force_umount_dev(MKDEV(DM_MAJOR, 3));
		force_umount_dev(MKDEV(SCSI_DISK_MAJOR, 1));
		force_umount_dev(MKDEV(SCSI_DISK_MAJOR, 0));
		sync_dev(MKDEV(SCSI_DISK_MAJOR, 0));
		sync_dev(MKDEV(SCSI_DISK_MAJOR, 1));
	}
	invalidate_buffers(MKDEV(SCSI_DISK_MAJOR, 0));
	invalidate_buffers(MKDEV(SCSI_DISK_MAJOR, 1));

	if (usb_sd_fd >= 0) {
		close(usb_sd_fd);
		usb_sd_fd = -1;
	}

	if (online) {
		const char *path = "./disk/x86/usb_ext2.img";
		if (fstype && strcmp(fstype, "ntfs") == 0)
			path = "./disk/x86/usb_ntfs.img";
		else if (fstype && strcmp(fstype, "ext4") == 0)
			path = "./disk/x86/usb_ext4.img";
		else if ((fstype && strcmp(fstype, "crypt") == 0) ||
			 (uuid && strcmp(uuid, "CRYPT-8A01") == 0))
			path = "./disk/x86/usb_crypt.img";

		strncpy(usb_sd_img_path, path, sizeof(usb_sd_img_path) - 1);
		usb_sd_img_path[sizeof(usb_sd_img_path) - 1] = '\0';

		usb_sd_fd = open(usb_sd_img_path, 2 | 0100, 0644); /* O_RDWR | O_CREAT */
		if (usb_sd_fd < 0) {
			printk("usb_sd: cannot open host image %s\n", usb_sd_img_path);
			usb_sd_online = 0;
			sd_sizes[0] = 0;
			sd_sizes[1] = 0;
			return;
		}
		{
			extern int ftruncate(int fd, unsigned long length);
			ftruncate(usb_sd_fd, USB_SD_SECTORS * 512UL);
		}

		usb_sd_online = 1;
		sd_sizes[0] = (int)(USB_SD_SECTORS >> (BLOCK_SIZE_BITS - 9));
		sd_sizes[1] = (int)(USB_SD_SECTORS >> (BLOCK_SIZE_BITS - 9));
		if (label && label[0]) {
			strncpy(usb_sd_label, label, sizeof(usb_sd_label) - 1);
			usb_sd_label[sizeof(usb_sd_label) - 1] = '\0';
		}
		if (uuid && uuid[0]) {
			strncpy(usb_sd_uuid, uuid, sizeof(usb_sd_uuid) - 1);
			usb_sd_uuid[sizeof(usb_sd_uuid) - 1] = '\0';
		}
		if (fstype && fstype[0]) {
			strncpy(usb_sd_fstype, fstype, sizeof(usb_sd_fstype) - 1);
			usb_sd_fstype[sizeof(usb_sd_fstype) - 1] = '\0';
		}
		if (!was_online || strcmp(prev_path, usb_sd_img_path) != 0) {
			printk("usb 1-1: new high-speed USB device number 2 using six_xhci (%s)\n",
			       usb_sd_img_path);
			printk("usb-storage 1-1:1.0: USB Mass Storage device detected\n");
			printk("sd 0:0:0:0: [sda] %lu 512-byte logical blocks (%lu KB)\n",
			       USB_SD_SECTORS, USB_SD_SECTORS >> 1);
			printk(" sda: sda1 (label=%s, uuid=%s, type=%s)\n",
			       usb_sd_label, usb_sd_uuid, usb_sd_fstype);
		}
	} else {
		usb_sd_online = 0;
		sd_sizes[0] = 0;
		sd_sizes[1] = 0;
		set_device_ro(MKDEV(SCSI_DISK_MAJOR, 0), 0);
		set_device_ro(MKDEV(SCSI_DISK_MAJOR, 1), 0);
		printk("usb 1-1: USB disconnect, device number 2 ([sda] detached)\n");
	}
}

int usb_sd_rw_sector(int minor, unsigned long phys_sec,
		     unsigned char *buf, int cmd)
{
	struct buffer_head *bh;
	unsigned long block_nr = phys_sec >> 1;
	int sub_off = (phys_sec & 1) << 9;
	kdev_t kdev = MKDEV(SCSI_DISK_MAJOR, minor);

	if (!usb_sd_is_online() || (minor != 0 && minor != 1))
		return -ENODEV;
	if (phys_sec >= USB_SD_SECTORS)
		return -EIO;

	bh = get_hash_table(kdev, block_nr, 1024);
	if (cmd == READ) {
		if (bh && buffer_uptodate(bh) && buffer_dirty(bh)) {
			memcpy(buf, bh->b_data + sub_off, 512);
			brelse(bh);
			return 0;
		}
		if (bh)
			brelse(bh);
		lseek(usb_sd_fd, (long)phys_sec * 512L, 0);
		if (read(usb_sd_fd, buf, 512) != 512)
			return -EIO;
		return 0;
	} else {
		if (is_read_only(kdev) || is_read_only(MKDEV(SCSI_DISK_MAJOR, 0))) {
			if (bh)
				brelse(bh);
			return -EROFS;
		}
		lseek(usb_sd_fd, (long)phys_sec * 512L, 0);
		if (write(usb_sd_fd, buf, 512) != 512) {
			if (bh)
				brelse(bh);
			return -EIO;
		}
		if (bh) {
			memcpy(bh->b_data + sub_off, buf, 512);
			brelse(bh);
		}
		return 0;
	}
}

static void end_request(int uptodate)
{
	struct request *req = CURRENT;
	struct buffer_head *bh;

	req->errors = 0;
	if (!uptodate) {
		req->nr_sectors--;
		req->nr_sectors &= ~SECTOR_MASK;
		req->sector += (BLOCK_SIZE / 512);
		req->sector &= ~SECTOR_MASK;
	}

	if ((bh = req->bh) != NULL) {
		req->bh = bh->b_reqnext;
		bh->b_reqnext = NULL;
		mark_buffer_uptodate(bh, uptodate);
		unlock_buffer(bh);
		if ((bh = req->bh) != NULL) {
			req->current_nr_sectors = bh->b_size >> 9;
			if (req->nr_sectors < req->current_nr_sectors)
				req->nr_sectors = req->current_nr_sectors;
			req->buffer = bh->b_data;
			return;
		}
	}
	CURRENT = req->next;
	if (req->sem != NULL)
		up(req->sem);
	req->rq_status = RQ_INACTIVE;
	wake_up(&wait_for_request);
}

void do_sd_request(void)
{
	int minor;
	unsigned long nsect, bytes;

	while (1) {
		INIT_REQUEST;

		minor = MINOR(CURRENT->rq_dev);
		if (!usb_sd_is_online() || (minor != 0 && minor != 1)) {
			end_request(0);
			continue;
		}

		nsect = CURRENT->current_nr_sectors;
		if (CURRENT->sector + nsect > USB_SD_SECTORS) {
			end_request(0);
			continue;
		}

		bytes = nsect << 9;
		lseek(usb_sd_fd, (long)CURRENT->sector * 512L, 0);
		if (CURRENT->cmd == READ) {
			if (read(usb_sd_fd, CURRENT->buffer, bytes) != (int)bytes) {
				end_request(0);
				continue;
			}
		} else if (CURRENT->cmd == WRITE) {
			if (is_read_only(CURRENT->rq_dev) ||
			    is_read_only(MKDEV(SCSI_DISK_MAJOR, 0))) {
				end_request(0);
				continue;
			}
			if (write(usb_sd_fd, CURRENT->buffer, bytes) != (int)bytes) {
				end_request(0);
				continue;
			}
		} else {
			end_request(0);
			continue;
		}

		CURRENT->sector += nsect;
		CURRENT->buffer += bytes;
		CURRENT->nr_sectors -= nsect;
		CURRENT->current_nr_sectors = 0;
		end_request(1);
	}
}

static int usb_sd_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	int minor;

	if (!inode)
		return -EINVAL;
	minor = MINOR(inode->i_rdev);
	if (!usb_sd_is_online() || (minor != 0 && minor != 1))
		return -ENODEV;

	switch (cmd) {
	case BLKGETSIZE:
		if (!arg)
			return -EINVAL;
		if (verify_area(VERIFY_WRITE, (long *)arg, sizeof(long)))
			return -EFAULT;
		put_user(USB_SD_SECTORS, (long *)arg);
		return 0;
	case BLKFLSBUF:
		fsync_dev(inode->i_rdev);
		invalidate_buffers(inode->i_rdev);
		return 0;
	RO_IOCTLS(inode->i_rdev, arg)
	default:
		return -EINVAL;
	}
}

static int usb_sd_open(struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev);
	if (!usb_sd_is_online() || (minor != 0 && minor != 1))
		return -ENODEV;
	if (file && (file->f_mode & 2) &&
	    (is_read_only(inode->i_rdev) || is_read_only(MKDEV(SCSI_DISK_MAJOR, 0))))
		return -EROFS;
	return 0;
}

static void usb_sd_release(struct inode *inode, struct file *file)
{
	if (inode)
		sync_dev(inode->i_rdev);
}

static struct file_operations usb_sd_fops = {
	NULL,			/* lseek - default */
	block_read,		/* read */
	block_write,		/* write */
	NULL,			/* readdir */
	NULL,			/* select */
	usb_sd_ioctl,		/* ioctl */
	NULL,			/* mmap */
	usb_sd_open,		/* open */
	usb_sd_release,		/* release */
	block_fsync,		/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL			/* revalidate */
};

int usb_sd_init(void)
{
	int i;

	if (register_blkdev(SCSI_DISK_MAJOR, "sd", &usb_sd_fops)) {
		printk("usb_sd: unable to register major %d\n", SCSI_DISK_MAJOR);
		return -1;
	}
	blk_dev[SCSI_DISK_MAJOR].request_fn = DEVICE_REQUEST;
	read_ahead[SCSI_DISK_MAJOR] = 8;
	for (i = 0; i < USB_SD_MAX_MINORS; i++) {
		sd_sizes[i] = 0;
		sd_blocksizes[i] = 1024;
	}
	blk_size[SCSI_DISK_MAJOR] = sd_sizes;
	blksize_size[SCSI_DISK_MAJOR] = sd_blocksizes;
	printk("usb_sd: simulated USB Mass Storage controller ready (/dev/sda, major %d)\n",
	       SCSI_DISK_MAJOR);
	return 0;
}
