/*
 * plugins/ufs/fu-ufs-plugin.c - JEDEC UFS 4.0 Field Firmware Update (FFU) Plugin
 *
 * Target: /dev/ufs-bsg0 (SIX JEDEC UFS 4.0 Flash Controller)
 * Protocol: org.jedec.ufs / org.t10.scsi.write_buffer
 *   - UPIU Query Request (READ_DESC Device Descriptor IDN 0x00, READ_ATTR bBootLunID 0x00)
 *   - UFS SCSI INQUIRY (0x12) + Extended 32-byte active microcode SHA-256
 *   - UFS SCSI WRITE_BUFFER (0x3B, Mode 0x0E Download Microcode with Offsets)
 *   - UFS SCSI WRITE_BUFFER (0x3B, Mode 0x0F Activate Deferred Microcode)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/ufs.h>
#include "../../fwupd_plugin.h"

static int fu_ufs_plugin_probe(struct fwupd_device *devs, int max_devs)
{
	struct ufs_bsg_scsi_ioctl sc;
	struct ufs_bsg_query_ioctl q;
	struct ufs_device_desc desc;
	unsigned char inq[68];
	char vendor[12], prod[20], rev[8];
	unsigned int boot_slot = 1;
	struct fwupd_device *d;
	int fd;

	if (max_devs < 1)
		return 0;

	fd = open("/dev/ufs-bsg0", O_RDWR);
	if (fd < 0)
		fd = open("/dev/ufs-bsg0", O_RDONLY);
	if (fd < 0)
		return 0;

	memset(inq, 0, sizeof(inq));
	memset(&sc, 0, sizeof(sc));
	sc.lun = 0;
	sc.opcode = UFS_SCSI_INQUIRY;
	sc.data_len = sizeof(inq);
	sc.data_addr = (unsigned long)inq;

	if (ioctl(fd, UFS_IOCTL_SCSI_CMD, &sc) < 0 || sc.status != 0x00) {
		close(fd);
		return 0;
	}

	memset(&desc, 0, sizeof(desc));
	memset(&q, 0, sizeof(q));
	q.opcode = UPIU_QUERY_OPCODE_READ_DESC;
	q.idn = UFS_DESC_IDN_DEVICE;
	q.buf_len = sizeof(desc);
	q.buf_addr = (unsigned long)&desc;
	ioctl(fd, UFS_IOCTL_QUERY, &q);

	memset(&q, 0, sizeof(q));
	q.opcode = UPIU_QUERY_OPCODE_READ_ATTR;
	q.idn = UFS_ATTR_IDN_BOOT_LUN_ID;
	if (ioctl(fd, UFS_IOCTL_QUERY, &q) == 0)
		boot_slot = q.value;
	close(fd);

	d = &devs[0];
	memset(d, 0, sizeof(*d));
	strcpy(d->name, "SIX JEDEC UFS 4.0 Flash Controller");
	strcpy(d->device_id, FWUPD_DEVID_UFS);
	strcpy(d->guid, FWUPD_GUID_UFS);
	strcpy(d->plugin, "ufs");
	strcpy(d->dev_node, "/dev/ufs-bsg0");

	fwupd_trim_spaces((const char *)(inq + 8), 8, vendor);
	fwupd_trim_spaces((const char *)(inq + 16), 16, prod);
	fwupd_trim_spaces((const char *)(inq + 32), 4, rev);

	sprintf(d->vendor, "%s (%s)", vendor, prod);
	if (desc.serial_number[0])
		strncpy(d->serial, desc.serial_number, sizeof(d->serial) - 1);
	else
		strcpy(d->serial, "SIX-UFS4-2026-0001");
	strcpy(d->version, rev);
	memcpy(d->sha256, inq + 36, 32);

	sprintf(d->bootloader_info,
		"bBootLunID=0x%02x (Boot Slot %c), wDeviceVersion=0x%04x",
		boot_slot, (boot_slot == 2) ? 'B' : 'A', desc.wDeviceVersion);
	strcpy(d->flags_str,
	       "internal|updatable|signed-payload|usable-during-update");
	d->online = 1;
	return 1;
}

static int fu_ufs_plugin_write_firmware(const struct fwupd_device *dev,
					const unsigned char *img,
					unsigned int img_len)
{
	struct ufs_bsg_scsi_ioctl sc;
	unsigned int offset = 0;
	unsigned int chunk_idx = 0;
	unsigned int total_chunks = (img_len + FWUPD_CHUNK_SIZE - 1) / FWUPD_CHUNK_SIZE;
	int fd, rc;

	fd = open(dev->dev_node, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "fwupdmgr [ufs]: cannot open %s\n", dev->dev_node);
		return -1;
	}

	/* Step 1: Stream microcode via UFS SCSI WRITE_BUFFER (0x3B, Mode 0x0E: Download with Offsets) */
	while (offset < img_len) {
		unsigned int chunk = img_len - offset;
		if (chunk > FWUPD_CHUNK_SIZE)
			chunk = FWUPD_CHUNK_SIZE;

		memset(&sc, 0, sizeof(sc));
		sc.lun = 0;
		sc.opcode = UFS_SCSI_WRITE_BUFFER;
		sc.rsvd = SCSI_WB_MODE_DOWNLOAD_OFFSET_SAVE;
		sc.lba = offset;
		sc.data_len = chunk;
		sc.data_addr = (unsigned long)(img + offset);

		if (ioctl(fd, UFS_IOCTL_SCSI_CMD, &sc) < 0 || sc.status != 0x00) {
			fprintf(stderr, "fwupdmgr [ufs]: WRITE_BUFFER (Mode 0x0E) failed at offset %u\n",
				offset);
			close(fd);
			return -1;
		}
		offset += chunk;
		chunk_idx++;
		printf("  [ufs] UFS FFU WRITE_BUFFER (0x3B, Mode 0x0E): chunk %u/%u (%u/%u bytes, offset=0x%04x)\n",
		       chunk_idx, total_chunks, offset, img_len, sc.lba);
	}

	/* Step 2: Activate deferred microcode via UFS SCSI WRITE_BUFFER (0x3B, Mode 0x0F) */
	memset(&sc, 0, sizeof(sc));
	sc.lun = 0;
	sc.opcode = UFS_SCSI_WRITE_BUFFER;
	sc.rsvd = SCSI_WB_MODE_ACTIVATE_DEFERRED;
	sc.lba = 0;
	sc.data_len = 0;
	sc.data_addr = 0;

	rc = ioctl(fd, UFS_IOCTL_SCSI_CMD, &sc);
	close(fd);
	if (rc < 0 || sc.status != 0x00) {
		printf("  [ufs] UFS FFU WRITE_BUFFER (0x3B, Mode 0x0F) REJECTED by controller (status=0x%02x CHECK_CONDITION)\n",
		       sc.status);
		return -1;
	}
	printf("  [ufs] UFS FFU WRITE_BUFFER (0x3B, Mode 0x0F): deferred microcode verified & activated\n");
	return 0;
}

const struct fwupd_plugin_ops fu_ufs_plugin_ops = {
	"ufs",
	"JEDEC UFS 4.0 Field Firmware Update (FFU) Plugin",
	"UFSHCI BSG UPIU Query + SCSI WRITE_BUFFER (0x3B Mode 0x0E & 0x0F)",
	fu_ufs_plugin_probe,
	fu_ufs_plugin_write_firmware,
	NULL
};
