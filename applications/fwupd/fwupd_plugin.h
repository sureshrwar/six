/*
 * fwupd_plugin.h - Modular Hardware Plugin ABI for SIX fwupd (/bin/fwupdmgr)
 *
 * Each hardware plugin lives under applications/fwupd/plugins/<name>/ and
 * exports a `struct fwupd_plugin_ops` descriptor registered with the core
 * fwupd engine:
 *   - plugins/nvme/fu-nvme-plugin.c : NVM Express 1.4 Admin Queue plugin
 *   - plugins/ufs/fu-ufs-plugin.c   : JEDEC UFS 4.0 UPIU + Field Firmware Update
 *   - plugins/scsi/fu-scsi-plugin.c : SPC-4 SCSI WRITE_BUFFER FFU plugin
 */

#ifndef _SIX_FWUPD_PLUGIN_H
#define _SIX_FWUPD_PLUGIN_H

#include <linux/fwupd.h>

struct fwupd_device {
	char		name[48];
	char		device_id[24];
	char		guid[40];
	char		guid2[40];
	char		update_protocol[32];
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

struct fwupd_plugin_ops {
	const char	*name;
	const char	*summary;
	const char	*protocol;
	int (*probe)(struct fwupd_device *devs, int max_devs);
	int (*write_firmware)(const struct fwupd_device *dev,
			      int bin_fd,
			      unsigned int bin_size);
	int (*activate_slot)(const struct fwupd_device *dev,
			     unsigned int slot);
};

/* Helpers exported by fwupdmgr.c for plugin string normalization & .bin/.cab I/O */
void fwupd_trim_spaces(const char *src, int max_len, char *dst);
int fwupd_payload_read(int fd, unsigned int offset,
		       unsigned char *buf, unsigned int len);

/* Registered built-in plugins under applications/fwupd/plugins/ */
extern const struct fwupd_plugin_ops fu_nvme_plugin_ops;
extern const struct fwupd_plugin_ops fu_ufs_plugin_ops;
extern const struct fwupd_plugin_ops fu_scsi_plugin_ops;

#endif /* _SIX_FWUPD_PLUGIN_H */
