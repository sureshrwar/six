/*
 * plugins/nvme/fu-nvme-plugin.c - NVM Express 1.4 Firmware Update Plugin
 *
 * Target: /dev/nvme0 (SIX Virtual NVMe SSD Controller)
 * Protocol: org.nvmexpress
 *   - Admin Identify Controller (opcode 0x06, CNS 0x01)
 *   - Admin Get Log Page: Firmware Slot Information (opcode 0x02, LID 0x03)
 *   - Admin Firmware Image Download (opcode 0x11, 512B streaming chunks)
 *   - Admin Firmware Commit / Activate (opcode 0x10, CA=3 immediate / CA=2 slot switch)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/nvme.h>
#include "../../fwupd_plugin.h"

static int fu_nvme_plugin_probe(struct fwupd_device *devs, int max_devs)
{
	struct nvme_id_ctrl ctrl;
	struct nvme_fw_slot_info_log fw_log;
	struct nvme_passthru_cmd cmd;
	struct fwupd_device *d;
	int fd;

	if (max_devs < 1)
		return 0;

	fd = open("/dev/nvme0", O_RDWR);
	if (fd < 0)
		fd = open("/dev/nvme0n1", O_RDONLY);
	if (fd < 0)
		return 0;

	memset(&ctrl, 0, sizeof(ctrl));
	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = nvme_admin_identify;
	cmd.addr = (unsigned long)&ctrl;
	cmd.data_len = sizeof(ctrl);
	cmd.cdw10 = NVME_ID_CNS_CTRL;
	if (ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd) < 0) {
		close(fd);
		return 0;
	}

	memset(&fw_log, 0, sizeof(fw_log));
	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = nvme_admin_get_log_page;
	cmd.nsid = 0xffffffffU;
	cmd.addr = (unsigned long)&fw_log;
	cmd.data_len = sizeof(fw_log);
	cmd.cdw10 = NVME_LOG_FW_SLOT | (((sizeof(fw_log) / 4) - 1) << 16);
	ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd);
	close(fd);

	d = &devs[0];
	memset(d, 0, sizeof(*d));
	fwupd_trim_spaces(ctrl.mn, 40, d->name);
	strcpy(d->device_id, FWUPD_DEVID_NVME);
	strcpy(d->guid, FWUPD_GUID_NVME);
	strcpy(d->plugin, "nvme");
	strcpy(d->dev_node, "/dev/nvme0");
	strcpy(d->vendor, "SIX (PCIe 0x1B36)");
	fwupd_trim_spaces(ctrl.sn, 20, d->serial);
	fwupd_trim_spaces(ctrl.fr, 8, d->version);

	d->active_slot = fw_log.afi & 0x07;
	if (d->active_slot < 1 || d->active_slot > 2)
		d->active_slot = 1;
	fwupd_trim_spaces(fw_log.frs[0], 8, d->slot1_ver);
	fwupd_trim_spaces(fw_log.frs[1], 8, d->slot2_ver);
	if (!d->slot2_ver[0])
		strcpy(d->slot2_ver, "empty");

	sprintf(d->bootloader_info, "Active Slot %u (Slot1=%s [RO], Slot2=%s [RW])",
		d->active_slot, d->slot1_ver, d->slot2_ver);
	strcpy(d->flags_str, "internal|updatable|signed-payload|usable-during-update|dual-image");
	memcpy(d->sha256, fw_log.slot_sha256[d->active_slot - 1], 32);
	d->online = 1;
	return 1;
}

static int fu_nvme_plugin_write_firmware(const struct fwupd_device *dev,
					 int bin_fd,
					 unsigned int bin_size)
{
	struct nvme_passthru_cmd cmd;
	unsigned char chunk_buf[FWUPD_CHUNK_SIZE];
	unsigned int offset = 0;
	unsigned int chunk_idx = 0;
	unsigned int total_chunks = (bin_size + FWUPD_CHUNK_SIZE - 1) / FWUPD_CHUNK_SIZE;
	int fd, rc;

	fd = open(dev->dev_node, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "fwupdmgr [nvme]: cannot open %s\n", dev->dev_node);
		return -1;
	}

	lseek(bin_fd, 0L, 0);

	/* Step 1: Stream .bin firmware image via NVME_ADMIN_DOWNLOAD_FW (opcode 0x11) */
	while (offset < bin_size) {
		unsigned int chunk = bin_size - offset;
		unsigned int padded_chunk;
		int nread;

		if (chunk > FWUPD_CHUNK_SIZE)
			chunk = FWUPD_CHUNK_SIZE;
		padded_chunk = (chunk + 3U) & ~3U;
		memset(chunk_buf, 0, sizeof(chunk_buf));
		nread = read(bin_fd, chunk_buf, chunk);
		if (nread <= 0) {
			close(fd);
			return -1;
		}

		memset(&cmd, 0, sizeof(cmd));
		cmd.opcode = nvme_admin_download_fw;
		cmd.addr = (unsigned long)chunk_buf;
		cmd.data_len = (unsigned int)nread;
		cmd.cdw10 = (padded_chunk / 4U) - 1U;	/* NUMD (0-based dwords) */
		cmd.cdw11 = offset / 4U;		/* OFST (dword offset) */
		cmd.cdw12 = (unsigned int)nread;

		if (ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd) < 0) {
			fprintf(stderr, "fwupdmgr [nvme]: NVME_ADMIN_DOWNLOAD_FW failed at offset %u\n",
				offset);
			close(fd);
			return -1;
		}
		offset += (unsigned int)nread;
		chunk_idx++;
		if (total_chunks <= 8 || chunk_idx <= 2 || chunk_idx == total_chunks) {
			printf("  [nvme] NVME_ADMIN_DOWNLOAD_FW (0x11): chunk %u/%u (%u/%u bytes, OFST=%u dwords)\n",
			       chunk_idx, total_chunks, offset, bin_size, cmd.cdw11);
		} else if (chunk_idx == 3) {
			printf("  [nvme] NVME_ADMIN_DOWNLOAD_FW (0x11): streaming %u intermediate chunks...\n",
			       total_chunks - 3);
		}
	}

	/* Step 2: Commit & Activate in Slot 2 via NVME_ADMIN_ACTIVATE_FW (opcode 0x10, CA=3, FS=2) */
	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = nvme_admin_activate_fw;
	cmd.cdw10 = (3U << 3) | 2U; /* CA=011b (activate immediately), FS=2 (Slot 2) */

	rc = ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd);
	close(fd);
	if (rc < 0) {
		printf("  [nvme] NVME_ADMIN_ACTIVATE_FW (0x10) REJECTED by controller (status=NVME_SC_FW_IMAGE_ERROR)\n");
		return -1;
	}
	printf("  [nvme] NVME_ADMIN_ACTIVATE_FW (0x10): committed & activated Slot 2 (CA=3, FS=2)\n");
	return 0;
}

static int fu_nvme_plugin_activate_slot(const struct fwupd_device *dev,
					unsigned int slot)
{
	struct nvme_passthru_cmd cmd;
	int fd, rc;

	fd = open(dev->dev_node, O_RDWR);
	if (fd < 0)
		return -1;

	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = nvme_admin_activate_fw;
	cmd.cdw10 = (2U << 3) | (slot & 0x07U); /* CA=010b (activate existing slot), FS=slot */
	rc = ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd);
	close(fd);
	return rc;
}

const struct fwupd_plugin_ops fu_nvme_plugin_ops = {
	"nvme",
	"NVM Express 1.4 Controller Firmware Update Plugin",
	"NVMe Admin Queue (0x11 Download / 0x10 Commit / LID 0x03 Slot Log)",
	fu_nvme_plugin_probe,
	fu_nvme_plugin_write_firmware,
	fu_nvme_plugin_activate_slot
};
