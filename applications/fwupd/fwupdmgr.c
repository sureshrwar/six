/*
 * fwupdmgr.c - Linux Vendor Firmware Service (LVFS) / fwupd Client & Plugin
 *              Engine for SIX (/bin/fwupdmgr)
 *
 * Built-in Hardware Plugins:
 *   1. nvme : NVMe 1.4 Admin Queue Firmware Image Download (0x11) &
 *             Firmware Commit / Activate (0x10) + Firmware Slot Log (LID 0x03)
 *             Target: /dev/nvme0 (SIX Virtual NVMe SSD Controller)
 *   2. scsi : SPC-4 SCSI INQUIRY (0x12), WRITE_BUFFER (0x3B, Mode 0x0E/0x0F),
 *             and READ_BUFFER (0x3C) Field Firmware Update (FFU)
 *             Targets: /dev/ufs-bsg0 (SIX JEDEC UFS 4.0 Flash Controller)
 *                      /dev/sda      (SIX USB Mass Storage SCSI Disk)
 *
 * Supported Subcommands:
 *   fwupdmgr get-plugins              List registered fwupd hardware plugins
 *   fwupdmgr get-devices              Probe all plugins and display device tree
 *   fwupdmgr refresh                  Refresh LVFS metadata & signed .fw capsules
 *   fwupdmgr get-updates              Compare device versions against LVFS catalog
 *   fwupdmgr update [DEVICE-ID|GUID]  Apply all pending LVFS firmware updates
 *   fwupdmgr install <file.fw> [--allow-older] [--allow-reinstall]
 *                                     Install a signed .fw firmware capsule
 *   fwupdmgr activate <DEVICE-ID> <slot>
 *                                     Switch active NVMe firmware slot (1 or 2)
 *   fwupdmgr verify [DEVICE-ID|GUID]  Verify device hardware SHA-256 checksums
 *   fwupdmgr get-history              Display firmware update history log
 *   fwupdmgr clear-history            Clear firmware update history log
 *   fwupdmgr examine <file.fw>        Inspect a .fw capsule header & SHA-256
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include "../../include/linux/nvme.h"
#include "../../include/linux/ufs.h"
#include "../../include/linux/fwupd.h"

#define MAX_DEVICES		8
#define LVFS_PKG_DIR		"/etc/fwupd/remotes.d/lvfs/packages"
#define LVFS_META_PATH		"/etc/fwupd/remotes.d/lvfs/metadata.xml"
#define FWUPD_HISTORY_PATH	"/var/lib/fwupd/history.db"

struct fwupd_device {
	char		name[48];
	char		device_id[24];
	char		guid[40];
	char		plugin[16];
	char		dev_node[32];
	char		vendor[40];
	char		serial[28];
	char		version[16];
	char		bootloader_info[64];
	char		flags_str[96];
	unsigned char	active_slot;
	char		slot1_ver[12];
	char		slot2_ver[12];
	unsigned char	sha256[32];
	int		online;
};

struct lvfs_release {
	const char	*component_id;
	const char	*device_id;
	const char	*guid;
	const char	*plugin;
	const char	*latest_ver;
	const char	*factory_ver;
	const char	*urgency;
	const char	*summary;
	const char	*pkg_filename;
	const char	*factory_pkg_filename;
	unsigned short	capsule_flags;
};

static const struct lvfs_release lvfs_catalog[] = {
	{
		"org.six.nvme.firmware",
		FWUPD_DEVID_NVME,
		FWUPD_GUID_NVME,
		"nvme",
		"1.4.2",
		"1.4.0",
		"High",
		"Optimize Admin/IO Queue doorbell latency and DSM/TRIM wear leveling",
		"/etc/fwupd/remotes.d/lvfs/packages/six-nvme-ssd-1.4.2.fw",
		"/etc/fwupd/remotes.d/lvfs/packages/six-nvme-ssd-1.4.0.fw",
		FWUPD_FLAG_SIGNED_PAYLOAD | FWUPD_FLAG_DUAL_IMAGE | FWUPD_FLAG_USABLE_DURING_UPDATE
	},
	{
		"org.six.ufs.firmware",
		FWUPD_DEVID_UFS,
		FWUPD_GUID_UFS,
		"scsi",
		"4.10",
		"4.00",
		"Medium",
		"Improve HS-Gear5 UniPro link stability and SLC WriteBooster flush policy",
		"/etc/fwupd/remotes.d/lvfs/packages/six-ufs-flash-4.10.fw",
		"/etc/fwupd/remotes.d/lvfs/packages/six-ufs-flash-4.00.fw",
		FWUPD_FLAG_SIGNED_PAYLOAD | FWUPD_FLAG_USABLE_DURING_UPDATE
	},
	{
		"org.six.usb_scsi.firmware",
		FWUPD_DEVID_USB_SCSI,
		FWUPD_GUID_USB_SCSI,
		"scsi",
		"1.10",
		"1.00",
		"Medium",
		"Fix USB Mass Storage BOT SCSI sense reporting on hotplug transitions",
		"/etc/fwupd/remotes.d/lvfs/packages/six-usb-scsi-1.10.fw",
		"/etc/fwupd/remotes.d/lvfs/packages/six-usb-scsi-1.00.fw",
		FWUPD_FLAG_SIGNED_PAYLOAD | FWUPD_FLAG_REMOVABLE
	}
};

#define NR_LVFS_RELEASES	(int)(sizeof(lvfs_catalog) / sizeof(lvfs_catalog[0]))

static void trim_spaces(const char *src, int max_len, char *dst)
{
	int i;

	memcpy(dst, src, max_len);
	dst[max_len] = '\0';
	for (i = max_len - 1; i >= 0; i--) {
		if (dst[i] == ' ' || dst[i] == '\0' || dst[i] == '\n' || dst[i] == '\r')
			dst[i] = '\0';
		else
			break;
	}
}

static void format_sha256_hex(const unsigned char sha256[32], char out_hex[65])
{
	static const char hex[] = "0123456789abcdef";
	int i;

	for (i = 0; i < 32; i++) {
		out_hex[i * 2 + 0] = hex[(sha256[i] >> 4) & 0x0f];
		out_hex[i * 2 + 1] = hex[sha256[i] & 0x0f];
	}
	out_hex[64] = '\0';
}

/*
 * Compare semantic version strings (e.g. "1.4.0" vs "1.4.2", "4.00" vs "4.10").
 * Returns <0 if a < b, 0 if a == b, >0 if a > b.
 */
static int compare_versions(const char *a, const char *b)
{
	const char *pa = a ? a : "";
	const char *pb = b ? b : "";

	while (*pa || *pb) {
		long va = 0, vb = 0;
		while (*pa >= '0' && *pa <= '9') {
			va = va * 10 + (*pa - '0');
			pa++;
		}
		while (*pb >= '0' && *pb <= '9') {
			vb = vb * 10 + (*pb - '0');
			pb++;
		}
		if (va != vb)
			return (va < vb) ? -1 : 1;
		if (*pa == '.')
			pa++;
		if (*pb == '.')
			pb++;
		if ((*pa && (*pa < '0' || *pa > '9')) ||
		    (*pb && (*pb < '0' || *pb > '9')))
			return strcmp(pa, pb);
	}
	return 0;
}

static int write_signed_capsule_file(const char *path, const char *plugin,
				     const char *device_id, const char *guid,
				     const char *fw_version, unsigned short flags)
{
	unsigned char buf[sizeof(struct fwupd_capsule_hdr) + FWUPD_DEFAULT_PAYLOAD_SIZE];
	struct fwupd_capsule_hdr *hdr = (struct fwupd_capsule_hdr *)buf;
	unsigned char *payload = buf + sizeof(struct fwupd_capsule_hdr);
	int fd;

	memset(buf, 0, sizeof(buf));
	hdr->magic = FWUPD_CAPSULE_MAGIC;
	hdr->hdr_version = FWUPD_CAPSULE_VERSION;
	hdr->flags = flags;
	strncpy(hdr->plugin, plugin, sizeof(hdr->plugin) - 1);
	strncpy(hdr->device_id, device_id, sizeof(hdr->device_id) - 1);
	strncpy(hdr->guid, guid, sizeof(hdr->guid) - 1);
	strncpy(hdr->fw_version, fw_version, sizeof(hdr->fw_version) - 1);
	hdr->payload_len = FWUPD_DEFAULT_PAYLOAD_SIZE;
	hdr->release_ts = 1790517600U; /* Sep 2026 */

	fwupd_build_microcode(device_id, fw_version, payload,
			      FWUPD_DEFAULT_PAYLOAD_SIZE, hdr->sha256);

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return -1;
	if (write(fd, buf, sizeof(buf)) != (int)sizeof(buf)) {
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

static void ensure_lvfs_repository(int force_rebuild)
{
	int i, fd;

	mkdir("/etc/fwupd", 0755);
	mkdir("/etc/fwupd/remotes.d", 0755);
	mkdir("/etc/fwupd/remotes.d/lvfs", 0755);
	mkdir(LVFS_PKG_DIR, 0755);
	mkdir("/var", 0755);
	mkdir("/var/lib", 0755);
	mkdir("/var/lib/fwupd", 0755);

	for (i = 0; i < NR_LVFS_RELEASES; i++) {
		const struct lvfs_release *r = &lvfs_catalog[i];
		fd = open(r->pkg_filename, O_RDONLY);
		if (fd >= 0 && !force_rebuild) {
			close(fd);
		} else {
			if (fd >= 0)
				close(fd);
			write_signed_capsule_file(r->pkg_filename, r->plugin,
						  r->device_id, r->guid,
						  r->latest_ver, r->capsule_flags);
		}

		fd = open(r->factory_pkg_filename, O_RDONLY);
		if (fd >= 0 && !force_rebuild) {
			close(fd);
		} else {
			if (fd >= 0)
				close(fd);
			write_signed_capsule_file(r->factory_pkg_filename, r->plugin,
						  r->device_id, r->guid,
						  r->factory_ver, r->capsule_flags);
		}
	}
}

/* =========================================================================
 * Plugin 1: NVMe 1.4 Plugin ("nvme")
 * Uses NVME_IOCTL_ADMIN_CMD with Identify (0x06), Get Log Page LID 0x03 (0x02),
 * Firmware Image Download (0x11), and Firmware Commit / Activate (0x10).
 * ========================================================================= */

static int nvme_plugin_probe(struct fwupd_device *devs, int max_devs)
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
	trim_spaces(ctrl.mn, 40, d->name);
	strcpy(d->device_id, FWUPD_DEVID_NVME);
	strcpy(d->guid, FWUPD_GUID_NVME);
	strcpy(d->plugin, "nvme");
	strcpy(d->dev_node, "/dev/nvme0");
	strcpy(d->vendor, "SIX (PCIe 0x1B36)");
	trim_spaces(ctrl.sn, 20, d->serial);
	trim_spaces(ctrl.fr, 8, d->version);

	d->active_slot = fw_log.afi & 0x07;
	if (d->active_slot < 1 || d->active_slot > 2)
		d->active_slot = 1;
	trim_spaces(fw_log.frs[0], 8, d->slot1_ver);
	trim_spaces(fw_log.frs[1], 8, d->slot2_ver);
	if (!d->slot2_ver[0])
		strcpy(d->slot2_ver, "empty");

	sprintf(d->bootloader_info, "Active Slot %u (Slot1=%s [RO], Slot2=%s [RW])",
		d->active_slot, d->slot1_ver, d->slot2_ver);
	strcpy(d->flags_str, "internal|updatable|signed-payload|usable-during-update|dual-image");
	memcpy(d->sha256, fw_log.slot_sha256[d->active_slot - 1], 32);
	d->online = 1;
	return 1;
}

static int nvme_plugin_write_firmware(const struct fwupd_device *dev,
				      const unsigned char *img,
				      unsigned int img_len)
{
	struct nvme_passthru_cmd cmd;
	unsigned int offset = 0;
	unsigned int chunk_idx = 0;
	unsigned int total_chunks = (img_len + FWUPD_CHUNK_SIZE - 1) / FWUPD_CHUNK_SIZE;
	int fd, rc;

	fd = open(dev->dev_node, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "fwupdmgr [nvme]: cannot open %s\n", dev->dev_node);
		return -1;
	}

	/* Step 1: Stream firmware capsule via NVME_ADMIN_DOWNLOAD_FW (opcode 0x11) */
	while (offset < img_len) {
		unsigned int chunk = img_len - offset;
		unsigned int padded_chunk;
		unsigned char chunk_buf[FWUPD_CHUNK_SIZE];

		if (chunk > FWUPD_CHUNK_SIZE)
			chunk = FWUPD_CHUNK_SIZE;
		padded_chunk = (chunk + 3U) & ~3U;
		memset(chunk_buf, 0, sizeof(chunk_buf));
		memcpy(chunk_buf, img + offset, chunk);

		memset(&cmd, 0, sizeof(cmd));
		cmd.opcode = nvme_admin_download_fw;
		cmd.addr = (unsigned long)chunk_buf;
		cmd.data_len = padded_chunk;
		cmd.cdw10 = (padded_chunk / 4U) - 1U;	/* NUMD (0-based dwords) */
		cmd.cdw11 = offset / 4U;		/* OFST (dword offset) */

		if (ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd) < 0) {
			fprintf(stderr, "fwupdmgr [nvme]: NVME_ADMIN_DOWNLOAD_FW failed at offset %u\n",
				offset);
			close(fd);
			return -1;
		}
		offset += chunk;
		chunk_idx++;
		printf("  [nvme] NVME_ADMIN_DOWNLOAD_FW (0x11): chunk %u/%u (%u/%u bytes, OFST=%u dwords)\n",
		       chunk_idx, total_chunks, offset, img_len, cmd.cdw11);
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

static int nvme_plugin_activate_slot(const struct fwupd_device *dev, unsigned int slot)
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

/* =========================================================================
 * Plugin 2: SPC-4 SCSI / UFS FFU Plugin ("scsi")
 * Uses UFS_IOCTL_SCSI_CMD with INQUIRY (0x12), WRITE_BUFFER (0x3B, Mode
 * 0x0E Download Microcode with Offsets & Mode 0x0F Activate Deferred
 * Microcode), and READ_BUFFER (0x3C) on /dev/ufs-bsg0 and /dev/sda.
 * ========================================================================= */

static int scsi_probe_one(const char *dev_node, const char *device_id,
			  const char *guid, const char *friendly_name,
			  const char *default_serial, int is_ufs,
			  struct fwupd_device *out_dev)
{
	struct ufs_bsg_scsi_ioctl sc;
	unsigned char inq[68];
	char vendor[12], prod[20], rev[8];
	int fd;

	fd = open(dev_node, O_RDWR);
	if (fd < 0)
		fd = open(dev_node, O_RDONLY);
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

	memset(out_dev, 0, sizeof(*out_dev));
	strcpy(out_dev->name, friendly_name);
	strcpy(out_dev->device_id, device_id);
	strcpy(out_dev->guid, guid);
	strcpy(out_dev->plugin, "scsi");
	strcpy(out_dev->dev_node, dev_node);

	trim_spaces((const char *)(inq + 8), 8, vendor);
	trim_spaces((const char *)(inq + 16), 16, prod);
	trim_spaces((const char *)(inq + 32), 4, rev);

	sprintf(out_dev->vendor, "%s (%s)", vendor, prod);
	strcpy(out_dev->serial, default_serial);
	strcpy(out_dev->version, rev);
	memcpy(out_dev->sha256, inq + 36, 32);

	if (is_ufs) {
		struct ufs_bsg_query_ioctl q;
		unsigned int boot_slot = 1;
		memset(&q, 0, sizeof(q));
		q.opcode = UPIU_QUERY_OPCODE_READ_ATTR;
		q.idn = UFS_ATTR_IDN_BOOT_LUN_ID;
		if (ioctl(fd, UFS_IOCTL_QUERY, &q) == 0)
			boot_slot = q.value;
		sprintf(out_dev->bootloader_info,
			"bBootLunID=0x%02x (Boot Slot %c)",
			boot_slot, (boot_slot == 2) ? 'B' : 'A');
		strcpy(out_dev->flags_str,
		       "internal|updatable|signed-payload|usable-during-update");
	} else {
		strcpy(out_dev->bootloader_info, "SPC-4 USB Mass Storage BOT");
		strcpy(out_dev->flags_str, "updatable|signed-payload|removable");
	}

	close(fd);
	out_dev->online = 1;
	return 1;
}

static int scsi_plugin_probe(struct fwupd_device *devs, int max_devs)
{
	int count = 0;

	if (count < max_devs) {
		count += scsi_probe_one("/dev/ufs-bsg0",
					FWUPD_DEVID_UFS,
					FWUPD_GUID_UFS,
					"SIX JEDEC UFS 4.0 Flash Controller",
					"SIX-UFS4-2026-0001",
					1,
					&devs[count]);
	}
	if (count < max_devs) {
		count += scsi_probe_one("/dev/sda",
					FWUPD_DEVID_USB_SCSI,
					FWUPD_GUID_USB_SCSI,
					"SIX USB Mass Storage SCSI Disk",
					"USB-SCSI-4A8F-9C21",
					0,
					&devs[count]);
	}
	return count;
}

static int scsi_plugin_write_firmware(const struct fwupd_device *dev,
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
		fprintf(stderr, "fwupdmgr [scsi]: cannot open %s\n", dev->dev_node);
		return -1;
	}

	/* Step 1: Stream microcode via SCSI WRITE_BUFFER (0x3B, Mode 0x0E: Download with Offsets) */
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
			fprintf(stderr, "fwupdmgr [scsi]: WRITE_BUFFER (Mode 0x0E) failed at offset %u\n",
				offset);
			close(fd);
			return -1;
		}
		offset += chunk;
		chunk_idx++;
		printf("  [scsi] SCSI WRITE_BUFFER (0x3B, Mode 0x0E): chunk %u/%u (%u/%u bytes, offset=0x%04x)\n",
		       chunk_idx, total_chunks, offset, img_len, sc.lba);
	}

	/* Step 2: Activate deferred microcode via SCSI WRITE_BUFFER (0x3B, Mode 0x0F) */
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
		printf("  [scsi] SCSI WRITE_BUFFER (0x3B, Mode 0x0F) REJECTED by target (status=0x%02x CHECK_CONDITION)\n",
		       sc.status);
		return -1;
	}
	printf("  [scsi] SCSI WRITE_BUFFER (0x3B, Mode 0x0F): deferred microcode verified & activated\n");
	return 0;
}

/* =========================================================================
 * Plugin Registry & Dispatch Table
 * ========================================================================= */

struct fwupd_plugin_ops {
	const char	*name;
	const char	*summary;
	const char	*protocol;
	int (*probe)(struct fwupd_device *devs, int max_devs);
	int (*write_firmware)(const struct fwupd_device *dev,
			      const unsigned char *img,
			      unsigned int img_len);
};

static const struct fwupd_plugin_ops fwupd_plugins[] = {
	{
		"nvme",
		"NVM Express 1.4 Controller Firmware Update Plugin",
		"NVMe Admin Queue (0x11 Download / 0x10 Commit / LID 0x03 Slot Log)",
		nvme_plugin_probe,
		nvme_plugin_write_firmware
	},
	{
		"scsi",
		"SPC-4 SCSI / JEDEC UFS 4.0 Field Firmware Update (FFU) Plugin",
		"SCSI CDB (0x12 INQUIRY / 0x3B WRITE_BUFFER Mode 0x0E & 0x0F / 0x3C READ_BUFFER)",
		scsi_plugin_probe,
		scsi_plugin_write_firmware
	}
};

#define NR_PLUGINS	(int)(sizeof(fwupd_plugins) / sizeof(fwupd_plugins[0]))

static int probe_all_devices(struct fwupd_device *devs, int max_devs)
{
	int total = 0;
	int i;

	for (i = 0; i < NR_PLUGINS && total < max_devs; i++) {
		total += fwupd_plugins[i].probe(devs + total, max_devs - total);
	}
	return total;
}

static const struct fwupd_plugin_ops *find_plugin(const char *name)
{
	int i;
	for (i = 0; i < NR_PLUGINS; i++) {
		if (strcmp(fwupd_plugins[i].name, name) == 0)
			return &fwupd_plugins[i];
	}
	return NULL;
}

static struct fwupd_device *find_device(struct fwupd_device *devs, int nr_devs,
					const char *query)
{
	int i;
	if (!query || !query[0])
		return NULL;
	for (i = 0; i < nr_devs; i++) {
		if (strcmp(devs[i].device_id, query) == 0 ||
		    strcmp(devs[i].guid, query) == 0 ||
		    strcmp(devs[i].dev_node, query) == 0 ||
		    strcmp(devs[i].plugin, query) == 0)
			return &devs[i];
	}
	return NULL;
}

static const struct lvfs_release *find_release_for_device(const char *device_id)
{
	int i;
	for (i = 0; i < NR_LVFS_RELEASES; i++) {
		if (strcmp(lvfs_catalog[i].device_id, device_id) == 0)
			return &lvfs_catalog[i];
	}
	return NULL;
}

static void record_history(const char *device_id, const char *guid,
			   const char *plugin, const char *old_ver,
			   const char *new_ver, const unsigned char sha256[32],
			   const char *status)
{
	char hex[65];
	char line[256];
	int fd;

	format_sha256_hex(sha256, hex);
	fd = open(FWUPD_HISTORY_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd < 0)
		return;
	sprintf(line, "device=%s guid=%s plugin=%s transition=%s->%s status=%s sha256=%.16s...\n",
		device_id, guid, plugin, old_ver, new_ver, status, hex);
	write(fd, line, (int)strlen(line));
	close(fd);
}

/* =========================================================================
 * CLI Subcommand Implementations
 * ========================================================================= */

static int cmd_get_plugins(void)
{
	int i;
	printf("fwupd 1.9.24 — Enabled Hardware Firmware Update Plugins:\n");
	for (i = 0; i < NR_PLUGINS; i++) {
		printf("  %-8s [ENABLED]  %s\n",
		       fwupd_plugins[i].name, fwupd_plugins[i].summary);
		printf("                      Transport: %s\n",
		       fwupd_plugins[i].protocol);
	}
	return 0;
}

static int cmd_get_devices(void)
{
	struct fwupd_device devs[MAX_DEVICES];
	int nr = probe_all_devices(devs, MAX_DEVICES);
	int i;

	printf("SIX Virtual Platform\n");
	for (i = 0; i < nr; i++) {
		char hex[65];
		const struct fwupd_device *d = &devs[i];
		const char *branch = (i == nr - 1) ? "└─" : "├─";
		const char *pipe   = (i == nr - 1) ? "  " : "│ ";

		format_sha256_hex(d->sha256, hex);
		printf("%s%s:\n", branch, d->name);
		printf("%s    Device ID:          %s\n", pipe, d->device_id);
		printf("%s    GUID:               %s\n", pipe, d->guid);
		printf("%s    Plugin:             %s (%s)\n", pipe, d->plugin, d->dev_node);
		printf("%s    Vendor / Serial:    %s / %s\n", pipe, d->vendor, d->serial);
		printf("%s    Current Version:    %s\n", pipe, d->version);
		printf("%s    Bootloader / Slots: %s\n", pipe, d->bootloader_info);
		printf("%s    Flags:              %s\n", pipe, d->flags_str);
		printf("%s    Device Checksum:    SHA256(%s)\n", pipe, hex);
	}
	return 0;
}

static int cmd_refresh(void)
{
	ensure_lvfs_repository(1);
	printf("Updating lvfs metadata from %s...\n", LVFS_META_PATH);
	printf("Successfully refreshed LVFS metadata (%d component releases, %d signed capsules staged in %s)\n",
	       NR_LVFS_RELEASES, NR_LVFS_RELEASES * 2, LVFS_PKG_DIR);
	return 0;
}

static int cmd_get_updates(void)
{
	struct fwupd_device devs[MAX_DEVICES];
	int nr = probe_all_devices(devs, MAX_DEVICES);
	int i, updates_found = 0;

	printf("Devices with available firmware updates:\n");
	for (i = 0; i < nr; i++) {
		const struct fwupd_device *d = &devs[i];
		const struct lvfs_release *r = find_release_for_device(d->device_id);
		if (!r)
			continue;
		if (compare_versions(d->version, r->latest_ver) < 0) {
			updates_found++;
			printf("  • %s (%s)\n", d->name, d->device_id);
			printf("      GUID:            %s\n", d->guid);
			printf("      Plugin / Node:   %s (%s)\n", d->plugin, d->dev_node);
			printf("      Current Version: %s\n", d->version);
			printf("      Update Version:  %s (Component: %s, Urgency: %s)\n",
			       r->latest_ver, r->component_id, r->urgency);
			printf("      Capsule Image:   %s\n", r->pkg_filename);
			printf("      Release Notes:   %s\n", r->summary);
		} else {
			printf("  ✓ %s (%s): up to date at version %s\n",
			       d->name, d->device_id, d->version);
		}
	}

	if (updates_found == 0)
		printf("No upgradable devices have pending updates.\n");
	return 0;
}

static int install_capsule_on_device(const char *capsule_path,
				     const char *target_query,
				     int allow_older,
				     int allow_reinstall)
{
	unsigned char buf[FWUPD_MAX_CAPSULE_SIZE];
	const struct fwupd_capsule_hdr *hdr = (const struct fwupd_capsule_hdr *)buf;
	unsigned char calc_sha256[32];
	struct fwupd_device devs[MAX_DEVICES];
	struct fwupd_device *dev;
	const struct fwupd_plugin_ops *plug;
	char old_ver[16], hex[65];
	int fd, nread, nr_devs, cmp;

	fd = open(capsule_path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "fwupdmgr: cannot open capsule file '%s'\n", capsule_path);
		return 1;
	}
	nread = read(fd, buf, sizeof(buf));
	close(fd);

	if (nread < (int)sizeof(struct fwupd_capsule_hdr)) {
		fprintf(stderr, "fwupdmgr: invalid capsule '%s' (truncated header)\n", capsule_path);
		return 1;
	}
	if (hdr->magic != FWUPD_CAPSULE_MAGIC || hdr->hdr_version != FWUPD_CAPSULE_VERSION) {
		fprintf(stderr, "fwupdmgr: invalid capsule magic 0x%08x in '%s'\n",
			hdr->magic, capsule_path);
		return 1;
	}
	if ((int)(sizeof(*hdr) + hdr->payload_len) > nread || hdr->payload_len == 0) {
		fprintf(stderr, "fwupdmgr: invalid capsule payload length %u in '%s'\n",
			hdr->payload_len, capsule_path);
		return 1;
	}

	nr_devs = probe_all_devices(devs, MAX_DEVICES);
	dev = find_device(devs, nr_devs, target_query ? target_query : hdr->device_id);
	if (!dev) {
		fprintf(stderr, "fwupdmgr: target device '%s' (%s) is not online\n",
			hdr->device_id, hdr->guid);
		return 1;
	}

	cmp = compare_versions(hdr->fw_version, dev->version);
	if (cmp == 0 && !allow_reinstall) {
		fprintf(stderr, "fwupdmgr: %s is already at version %s (use --allow-reinstall)\n",
			dev->device_id, dev->version);
		return 1;
	}
	if (cmp < 0 && !allow_older) {
		fprintf(stderr, "fwupdmgr: capsule version %s is older than installed %s on %s (use --allow-older)\n",
			hdr->fw_version, dev->version, dev->device_id);
		return 1;
	}

	plug = find_plugin(dev->plugin);
	if (!plug) {
		fprintf(stderr, "fwupdmgr: plugin '%s' not found\n", dev->plugin);
		return 1;
	}

	strcpy(old_ver, dev->version);
	format_sha256_hex(hdr->sha256, hex);
	fwupd_sha256(buf + sizeof(*hdr), hdr->payload_len, calc_sha256);

	printf("Decompressing & verifying capsule %s...\n", capsule_path);
	printf("  Target Device : %s (%s, GUID %s)\n", dev->name, dev->device_id, dev->guid);
	printf("  Plugin / Node : %s (%s)\n", dev->plugin, dev->dev_node);
	printf("  Transition    : %s -> %s (%u bytes payload)\n",
	       old_ver, hdr->fw_version, hdr->payload_len);
	printf("  Capsule Digest: SHA256(%.32s...)\n", hex);

	if (memcmp(calc_sha256, hdr->sha256, 32) != 0) {
		printf("  [WARN] Capsule payload SHA-256 mismatch detected in user-space; forwarding to controller to verify hardware rejection...\n");
	}

	if (plug->write_firmware(dev, buf, (unsigned int)nread) < 0) {
		record_history(dev->device_id, dev->guid, dev->plugin,
			       old_ver, hdr->fw_version, hdr->sha256, "failed-signature");
		printf("fwupdmgr: firmware update FAILED on %s (hardware rejected image)\n",
		       dev->device_id);
		return 1;
	}

	/* Re-probe device to verify the new version and hardware SHA-256 digest */
	nr_devs = probe_all_devices(devs, MAX_DEVICES);
	dev = find_device(devs, nr_devs, hdr->device_id);
	if (!dev || strcmp(dev->version, hdr->fw_version) != 0 ||
	    memcmp(dev->sha256, hdr->sha256, 32) != 0) {
		fprintf(stderr, "fwupdmgr: post-update verification failed on %s\n", hdr->device_id);
		return 1;
	}

	record_history(dev->device_id, dev->guid, dev->plugin,
		       old_ver, dev->version, dev->sha256, "success");
	printf("Successfully updated %s (%s) from %s to %s [VERIFIED]\n",
	       dev->name, dev->device_id, old_ver, dev->version);
	return 0;
}

static int cmd_update(const char *target_query)
{
	struct fwupd_device devs[MAX_DEVICES];
	int nr = probe_all_devices(devs, MAX_DEVICES);
	int i, updated = 0, rc = 0;

	for (i = 0; i < nr; i++) {
		const struct fwupd_device *d = &devs[i];
		const struct lvfs_release *r;

		if (target_query && target_query[0]) {
			if (strcmp(d->device_id, target_query) != 0 &&
			    strcmp(d->guid, target_query) != 0 &&
			    strcmp(d->plugin, target_query) != 0 &&
			    strcmp(d->dev_node, target_query) != 0)
				continue;
		}

		r = find_release_for_device(d->device_id);
		if (!r)
			continue;
		if (compare_versions(d->version, r->latest_ver) < 0) {
			if (install_capsule_on_device(r->pkg_filename, d->device_id, 0, 0) != 0)
				rc = 1;
			else
				updated++;
		}
	}

	if (updated == 0 && rc == 0)
		printf("All matching devices are already up to date.\n");
	return rc;
}

static int cmd_verify(const char *target_query)
{
	struct fwupd_device devs[MAX_DEVICES];
	unsigned char expected_payload[FWUPD_DEFAULT_PAYLOAD_SIZE];
	unsigned char expected_sha256[32];
	int nr = probe_all_devices(devs, MAX_DEVICES);
	int i, verified = 0;

	for (i = 0; i < nr; i++) {
		const struct fwupd_device *d = &devs[i];
		char hex[65];

		if (target_query && target_query[0]) {
			if (strcmp(d->device_id, target_query) != 0 &&
			    strcmp(d->guid, target_query) != 0 &&
			    strcmp(d->plugin, target_query) != 0)
				continue;
		}

		fwupd_build_microcode(d->device_id, d->version,
				      expected_payload, FWUPD_DEFAULT_PAYLOAD_SIZE,
				      expected_sha256);
		format_sha256_hex(d->sha256, hex);

		if (memcmp(d->sha256, expected_sha256, 32) == 0) {
			printf("Verified %s (%s, plugin=%s, version=%s): SHA256(%s) [OK]\n",
			       d->name, d->device_id, d->plugin, d->version, hex);
			verified++;
		} else {
			printf("Verification FAILED for %s (%s, version=%s)\n",
			       d->name, d->device_id, d->version);
			return 1;
		}
	}
	return (verified > 0) ? 0 : 1;
}

static int cmd_activate(const char *target_query, const char *slot_str)
{
	struct fwupd_device devs[MAX_DEVICES];
	struct fwupd_device *dev;
	unsigned int slot = slot_str ? (unsigned int)atoi(slot_str) : 1;
	char old_ver[16];
	int nr = probe_all_devices(devs, MAX_DEVICES);

	dev = find_device(devs, nr, target_query ? target_query : FWUPD_DEVID_NVME);
	if (!dev) {
		fprintf(stderr, "fwupdmgr activate: device not found\n");
		return 1;
	}
	if (strcmp(dev->plugin, "nvme") != 0) {
		fprintf(stderr, "fwupdmgr activate: dual-slot activation is only supported on nvme devices\n");
		return 1;
	}
	if (slot < 1 || slot > 2) {
		fprintf(stderr, "fwupdmgr activate: slot must be 1 or 2\n");
		return 1;
	}

	strcpy(old_ver, dev->version);
	if (nvme_plugin_activate_slot(dev, slot) < 0) {
		fprintf(stderr, "fwupdmgr activate: failed to activate Slot %u on %s\n",
			slot, dev->device_id);
		return 1;
	}

	nr = probe_all_devices(devs, MAX_DEVICES);
	dev = find_device(devs, nr, FWUPD_DEVID_NVME);
	if (dev) {
		record_history(dev->device_id, dev->guid, dev->plugin,
			       old_ver, dev->version, dev->sha256, "slot-switched");
		printf("Switched %s (%s) active firmware slot to Slot %u (version %s)\n",
		       dev->name, dev->device_id, dev->active_slot, dev->version);
	}
	return 0;
}

static int cmd_get_history(void)
{
	FILE *fp = fopen(FWUPD_HISTORY_PATH, "r");
	char line[256];
	int count = 0;

	if (!fp) {
		printf("No firmware update history recorded yet (%s).\n", FWUPD_HISTORY_PATH);
		return 0;
	}
	printf("Firmware Update History (%s):\n", FWUPD_HISTORY_PATH);
	while (fgets(line, sizeof(line), fp)) {
		printf("  [%d] %s", ++count, line);
	}
	fclose(fp);
	if (count == 0)
		printf("  (empty)\n");
	return 0;
}

static int cmd_clear_history(void)
{
	unlink(FWUPD_HISTORY_PATH);
	printf("Cleared firmware update history (%s).\n", FWUPD_HISTORY_PATH);
	return 0;
}

static int cmd_examine(const char *capsule_path)
{
	unsigned char buf[FWUPD_MAX_CAPSULE_SIZE];
	const struct fwupd_capsule_hdr *hdr = (const struct fwupd_capsule_hdr *)buf;
	unsigned char calc_sha256[32];
	char hdr_hex[65], calc_hex[65];
	int fd, nread, sig_ok;

	if (!capsule_path) {
		fprintf(stderr, "fwupdmgr examine: missing capsule file path\n");
		return 1;
	}
	fd = open(capsule_path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "fwupdmgr examine: cannot open '%s'\n", capsule_path);
		return 1;
	}
	nread = read(fd, buf, sizeof(buf));
	close(fd);

	if (nread < (int)sizeof(struct fwupd_capsule_hdr) || hdr->magic != FWUPD_CAPSULE_MAGIC) {
		fprintf(stderr, "fwupdmgr examine: '%s' is not a valid FWUP capsule\n", capsule_path);
		return 1;
	}

	fwupd_sha256(buf + sizeof(*hdr), hdr->payload_len, calc_sha256);
	format_sha256_hex(hdr->sha256, hdr_hex);
	format_sha256_hex(calc_sha256, calc_hex);
	sig_ok = (memcmp(hdr->sha256, calc_sha256, 32) == 0);

	printf("Firmware Capsule Inspection (%s):\n", capsule_path);
	printf("  Magic / HdrVer : 0x%08x (\"FWUP\") / v%u\n", hdr->magic, hdr->hdr_version);
	printf("  Target Plugin  : %s\n", hdr->plugin);
	printf("  Target Device  : %s\n", hdr->device_id);
	printf("  Target GUID    : %s\n", hdr->guid);
	printf("  Target Version : %s\n", hdr->fw_version);
	printf("  Payload Size   : %u bytes (total capsule %d bytes)\n", hdr->payload_len, nread);
	printf("  Header SHA256  : %s\n", hdr_hex);
	printf("  Payload SHA256 : %s [%s]\n", calc_hex, sig_ok ? "VALID SIGNATURE" : "CORRUPTED");
	return sig_ok ? 0 : 1;
}

static void usage(void)
{
	printf("fwupdmgr 1.9.24 (SIX Firmware Update Manager — nvme & scsi plugins)\n"
	       "Usage: fwupdmgr <command> [options]\n\n"
	       "Commands:\n"
	       "  get-plugins                            List built-in hardware plugins (nvme, scsi)\n"
	       "  get-devices                            Probe hardware and list updatable devices\n"
	       "  refresh                                Refresh LVFS metadata and signed capsules\n"
	       "  get-updates                            Show available LVFS firmware updates\n"
	       "  update [DEVICE-ID|GUID|PLUGIN]         Update devices to latest LVFS firmware\n"
	       "  install <capsule.fw> [DEVICE-ID] [--allow-older] [--allow-reinstall]\n"
	       "                                         Install a signed .fw firmware capsule\n"
	       "  activate <DEVICE-ID> <slot>            Switch active NVMe firmware slot (1 or 2)\n"
	       "  verify [DEVICE-ID|GUID]                Verify device firmware SHA-256 checksums\n"
	       "  examine <capsule.fw>                   Inspect a .fw capsule header & SHA-256\n"
	       "  get-history                            Show firmware update history\n"
	       "  clear-history                          Clear firmware update history\n");
}

int main(int argc, char **argv)
{
	const char *sub;

	ensure_lvfs_repository(0);

	if (argc < 2) {
		usage();
		return 1;
	}

	sub = argv[1];
	if (strcmp(sub, "get-plugins") == 0 || strcmp(sub, "plugins") == 0)
		return cmd_get_plugins();
	if (strcmp(sub, "get-devices") == 0 || strcmp(sub, "devices") == 0)
		return cmd_get_devices();
	if (strcmp(sub, "refresh") == 0)
		return cmd_refresh();
	if (strcmp(sub, "get-updates") == 0 || strcmp(sub, "updates") == 0)
		return cmd_get_updates();
	if (strcmp(sub, "update") == 0 || strcmp(sub, "upgrade") == 0)
		return cmd_update((argc >= 3 && argv[2][0] != '-') ? argv[2] : NULL);
	if (strcmp(sub, "install") == 0) {
		const char *cap_path = NULL;
		const char *target = NULL;
		int allow_older = 0, allow_reinstall = 0;
		int i;
		for (i = 2; i < argc; i++) {
			if (strcmp(argv[i], "--allow-older") == 0)
				allow_older = 1;
			else if (strcmp(argv[i], "--allow-reinstall") == 0)
				allow_reinstall = 1;
			else if (!cap_path)
				cap_path = argv[i];
			else if (!target)
				target = argv[i];
		}
		if (!cap_path) {
			fprintf(stderr, "fwupdmgr install: missing <capsule.fw> path\n");
			return 1;
		}
		return install_capsule_on_device(cap_path, target, allow_older, allow_reinstall);
	}
	if (strcmp(sub, "activate") == 0 || strcmp(sub, "switch-slot") == 0) {
		const char *dev_q = (argc >= 3) ? argv[2] : FWUPD_DEVID_NVME;
		const char *slot_s = (argc >= 4) ? argv[3] : "1";
		return cmd_activate(dev_q, slot_s);
	}
	if (strcmp(sub, "verify") == 0)
		return cmd_verify((argc >= 3 && argv[2][0] != '-') ? argv[2] : NULL);
	if (strcmp(sub, "examine") == 0 || strcmp(sub, "inspect") == 0)
		return cmd_examine((argc >= 3) ? argv[2] : NULL);
	if (strcmp(sub, "get-history") == 0 || strcmp(sub, "history") == 0)
		return cmd_get_history();
	if (strcmp(sub, "clear-history") == 0)
		return cmd_clear_history();
	if (strcmp(sub, "-h") == 0 || strcmp(sub, "--help") == 0 || strcmp(sub, "help") == 0) {
		usage();
		return 0;
	}

	fprintf(stderr, "fwupdmgr: unknown command '%s'\n", sub);
	usage();
	return 1;
}
