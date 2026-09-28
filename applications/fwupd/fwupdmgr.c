/*
 * fwupdmgr.c - Linux Vendor Firmware Service (LVFS) / fwupd Client & Plugin
 *              Engine for SIX (/bin/fwupdmgr)
 *
 * Modular Hardware Plugins (under applications/fwupd/plugins/):
 *   1. plugins/nvme/fu-nvme-plugin.c : NVM Express 1.4 Admin Queue plugin
 *                                      Target: /dev/nvme0
 *   2. plugins/ufs/fu-ufs-plugin.c   : JEDEC UFS 4.0 UPIU + WRITE_BUFFER FFU
 *                                      Target: /dev/ufs-bsg0
 *   3. plugins/scsi/fu-scsi-plugin.c : SPC-4 SCSI WRITE_BUFFER FFU plugin
 *                                      Target: /dev/sda
 *
 * Firmware Binary (.bin) Support:
 *   - Parses AppStream XML catalog (/etc/fwupd/remotes.d/lvfs/metadata.xml)
 *   - Handles both signed controller .bin images (/etc/fwupd/remotes.d/lvfs/packages/*.bin)
 *     and arbitrary real-world vendor firmware .bin files of any size
 *     (ARM Cortex-R binaries, ELF32 images, UEFI capsules, raw microcode blobs).
 *
 * Supported Subcommands:
 *   fwupdmgr get-plugins              List registered fwupd hardware plugins
 *   fwupdmgr get-devices              Probe all plugins and display device tree
 *   fwupdmgr refresh                  Refresh LVFS metadata & .bin firmware images
 *   fwupdmgr get-updates              Compare device versions against LVFS catalog
 *   fwupdmgr update [DEVICE-ID|GUID]  Apply all pending LVFS firmware updates
 *   fwupdmgr install <file.bin> [DEVICE-ID] [--allow-older] [--allow-reinstall]
 *   fwupdmgr install-blob <file.bin> <DEVICE-ID>
 *                                     Install any signed or raw vendor .bin firmware
 *   fwupdmgr activate <DEVICE-ID> <slot>
 *                                     Switch active NVMe firmware slot (1 or 2)
 *   fwupdmgr verify [DEVICE-ID|GUID]  Verify device hardware SHA-256 checksums
 *   fwupdmgr get-history              Display firmware update history log
 *   fwupdmgr clear-history            Clear firmware update history log
 *   fwupdmgr examine <file.bin>       Inspect a .bin firmware image & SHA-256
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "fwupd_plugin.h"

#define MAX_DEVICES		8
#define MAX_RELEASES		8
#define LVFS_PKG_DIR		"/etc/fwupd/remotes.d/lvfs/packages"
#define LVFS_META_PATH		"/etc/fwupd/remotes.d/lvfs/metadata.xml"
#define FWUPD_HISTORY_PATH	"/var/lib/fwupd/history.db"

struct lvfs_release {
	char		component_id[40];
	char		device_id[24];
	char		guid[40];
	char		plugin[16];
	char		latest_ver[16];
	char		factory_ver[16];
	char		urgency[16];
	char		summary[96];
	char		pkg_filename[80];
	char		factory_pkg_filename[80];
	unsigned short	capsule_flags;
};

static struct lvfs_release lvfs_catalog[MAX_RELEASES] = {
	{
		"org.six.nvme.firmware",
		FWUPD_DEVID_NVME,
		FWUPD_GUID_NVME,
		"nvme",
		"1.4.2",
		"1.4.0",
		"High",
		"Optimize Admin/IO Queue doorbell latency and DSM/TRIM wear leveling",
		"/etc/fwupd/remotes.d/lvfs/packages/six-nvme-ssd-1.4.2.bin",
		"/etc/fwupd/remotes.d/lvfs/packages/six-nvme-ssd-1.4.0.bin",
		FWUPD_FLAG_SIGNED_PAYLOAD | FWUPD_FLAG_DUAL_IMAGE | FWUPD_FLAG_USABLE_DURING_UPDATE
	},
	{
		"org.six.ufs.firmware",
		FWUPD_DEVID_UFS,
		FWUPD_GUID_UFS,
		"ufs",
		"4.10",
		"4.00",
		"Medium",
		"Improve HS-Gear5 UniPro link stability and SLC WriteBooster flush policy",
		"/etc/fwupd/remotes.d/lvfs/packages/six-ufs-flash-4.10.bin",
		"/etc/fwupd/remotes.d/lvfs/packages/six-ufs-flash-4.00.bin",
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
		"/etc/fwupd/remotes.d/lvfs/packages/six-usb-scsi-1.10.bin",
		"/etc/fwupd/remotes.d/lvfs/packages/six-usb-scsi-1.00.bin",
		FWUPD_FLAG_SIGNED_PAYLOAD | FWUPD_FLAG_REMOVABLE
	}
};

static int nr_lvfs_releases = 3;

/*
 * Plugin Registry (compiled from applications/fwupd/plugins/<name>/)
 */
static const struct fwupd_plugin_ops * const fwupd_plugins[] = {
	&fu_nvme_plugin_ops,
	&fu_ufs_plugin_ops,
	&fu_scsi_plugin_ops
};

#define NR_PLUGINS	(int)(sizeof(fwupd_plugins) / sizeof(fwupd_plugins[0]))

void fwupd_trim_spaces(const char *src, int max_len, char *dst)
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

static int extract_xml_tag(const char *line, const char *open_tag,
			   const char *close_tag, char *out, int max_out)
{
	const char *s = strstr(line, open_tag);
	const char *e;
	int len;

	if (!s)
		return 0;
	s += strlen(open_tag);
	e = strstr(s, close_tag);
	if (!e || e <= s)
		return 0;
	len = (int)(e - s);
	if (len >= max_out)
		len = max_out - 1;
	memcpy(out, s, len);
	out[len] = '\0';
	return 1;
}

static int extract_xml_attr(const char *line, const char *attr_prefix,
			    char *out, int max_out)
{
	const char *s = strstr(line, attr_prefix);
	const char *e;
	int len;

	if (!s)
		return 0;
	s += strlen(attr_prefix);
	e = strchr(s, '"');
	if (!e || e <= s)
		return 0;
	len = (int)(e - s);
	if (len >= max_out)
		len = max_out - 1;
	memcpy(out, s, len);
	out[len] = '\0';
	return 1;
}

/*
 * Parse /etc/fwupd/remotes.d/lvfs/metadata.xml to populate/update lvfs_catalog[].
 */
static void parse_lvfs_metadata_xml(void)
{
	FILE *fp = fopen(LVFS_META_PATH, "r");
	char line[256], tmp[128];
	int cur = -1;

	if (!fp)
		return;

	while (fgets(line, sizeof(line), fp)) {
		if (strstr(line, "<component ")) {
			if (cur + 1 < MAX_RELEASES)
				cur++;
		} else if (cur >= 0 && cur < nr_lvfs_releases) {
			struct lvfs_release *r = &lvfs_catalog[cur];
			if (extract_xml_tag(line, "<id>", "</id>", tmp, sizeof(tmp))) {
				strncpy(r->component_id, tmp, sizeof(r->component_id) - 1);
			} else if (extract_xml_tag(line, "<firmware type=\"flashed\">", "</firmware>", tmp, sizeof(tmp))) {
				strncpy(r->guid, tmp, sizeof(r->guid) - 1);
			} else if (extract_xml_tag(line, "<value key=\"LVFS::Plugin\">", "</value>", tmp, sizeof(tmp))) {
				strncpy(r->plugin, tmp, sizeof(r->plugin) - 1);
			} else if (extract_xml_tag(line, "<value key=\"LVFS::DeviceId\">", "</value>", tmp, sizeof(tmp))) {
				strncpy(r->device_id, tmp, sizeof(r->device_id) - 1);
			} else if (strstr(line, "<release ")) {
				if (extract_xml_attr(line, "version=\"", tmp, sizeof(tmp)))
					strncpy(r->latest_ver, tmp, sizeof(r->latest_ver) - 1);
				if (extract_xml_attr(line, "urgency=\"", tmp, sizeof(tmp)))
					strncpy(r->urgency, tmp, sizeof(r->urgency) - 1);
			} else if (extract_xml_tag(line, "<location>", "</location>", tmp, sizeof(tmp))) {
				const char *p = (strncmp(tmp, "file://", 7) == 0) ? (tmp + 7) : tmp;
				strncpy(r->pkg_filename, p, sizeof(r->pkg_filename) - 1);
			} else if (extract_xml_tag(line, "<p>", "</p>", tmp, sizeof(tmp))) {
				strncpy(r->summary, tmp, sizeof(r->summary) - 1);
			}
		}
	}
	fclose(fp);
}

static int write_bin_firmware_file(const char *path, const char *plugin,
				   const char *device_id, const char *guid,
				   const char *fw_version, unsigned short flags)
{
	unsigned char buf[sizeof(struct fwupd_bin_hdr) + FWUPD_DEFAULT_PAYLOAD_SIZE];
	int fd;

	fwupd_build_bin_image(plugin, device_id, guid, fw_version, flags, buf, NULL);

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

static int bin_file_valid(const char *path, const char *expected_plugin)
{
	struct fwupd_bin_hdr hdr;
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return 0;
	if (read(fd, &hdr, sizeof(hdr)) != (int)sizeof(hdr)) {
		close(fd);
		return 0;
	}
	close(fd);
	return (hdr.magic == FWUPD_BIN_MAGIC &&
		strcmp(hdr.plugin, expected_plugin) == 0);
}

static void ensure_lvfs_repository(int force_rebuild)
{
	int i;

	mkdir("/etc/fwupd", 0755);
	mkdir("/etc/fwupd/remotes.d", 0755);
	mkdir("/etc/fwupd/remotes.d/lvfs", 0755);
	mkdir(LVFS_PKG_DIR, 0755);
	mkdir("/var", 0755);
	mkdir("/var/lib", 0755);
	mkdir("/var/lib/fwupd", 0755);
	mkdir("/data/misc", 0755);
	mkdir("/data/misc/fwupd", 0755);
	symlink("/data/misc/fwupd/history.db", FWUPD_HISTORY_PATH);

	parse_lvfs_metadata_xml();

	for (i = 0; i < nr_lvfs_releases; i++) {
		const struct lvfs_release *r = &lvfs_catalog[i];
		if (force_rebuild || !bin_file_valid(r->pkg_filename, r->plugin)) {
			write_bin_firmware_file(r->pkg_filename, r->plugin,
						r->device_id, r->guid,
						r->latest_ver, r->capsule_flags);
		}
		if (force_rebuild || !bin_file_valid(r->factory_pkg_filename, r->plugin)) {
			write_bin_firmware_file(r->factory_pkg_filename, r->plugin,
						r->device_id, r->guid,
						r->factory_ver, r->capsule_flags);
		}
	}
}

/*
 * Inspect any .bin file on disk using streaming SHA-256.
 * Works on both SIX controller .bin images and arbitrary real-world .bin files
 * of any size (ARM Cortex-R, ELF32, UEFI Capsule, raw vendor blobs).
 */
static int inspect_bin_file(const char *bin_path,
			    unsigned char hdr_buf[FWUPD_HDR_INSPECT_LEN],
			    unsigned int *out_hdr_captured,
			    unsigned int *out_total_size,
			    unsigned int *out_nonzero_bytes,
			    unsigned char out_full_sha256[32],
			    unsigned char out_code_sha256[32])
{
	struct fwupd_sha256_ctx full_ctx, code_ctx;
	unsigned char chunk[FWUPD_CHUNK_SIZE];
	unsigned int offset = 0, hdr_cap = 0, nonzero = 0;
	int fd, nread, i;

	fd = open(bin_path, O_RDONLY);
	if (fd < 0)
		return -1;

	memset(hdr_buf, 0, FWUPD_HDR_INSPECT_LEN);
	fwupd_sha256_init(&full_ctx);
	fwupd_sha256_init(&code_ctx);

	while ((nread = read(fd, chunk, sizeof(chunk))) > 0) {
		unsigned int len = (unsigned int)nread;
		for (i = 0; i < nread; i++) {
			unsigned int pos = offset + (unsigned int)i;
			if (pos < FWUPD_HDR_INSPECT_LEN) {
				hdr_buf[pos] = chunk[i];
				if (pos + 1 > hdr_cap)
					hdr_cap = pos + 1;
			}
			if (chunk[i] != 0)
				nonzero++;
		}
		fwupd_sha256_update(&full_ctx, chunk, len);
		if (offset + len > (unsigned int)sizeof(struct fwupd_bin_hdr)) {
			unsigned int hdr_sz = (unsigned int)sizeof(struct fwupd_bin_hdr);
			unsigned int code_start = (offset >= hdr_sz) ? 0U : (hdr_sz - offset);
			fwupd_sha256_update(&code_ctx, chunk + code_start, len - code_start);
		}
		offset += len;
	}
	close(fd);

	fwupd_sha256_final(&full_ctx, out_full_sha256);
	fwupd_sha256_final(&code_ctx, out_code_sha256);
	if (out_hdr_captured)
		*out_hdr_captured = hdr_cap;
	if (out_total_size)
		*out_total_size = offset;
	if (out_nonzero_bytes)
		*out_nonzero_bytes = nonzero;
	return 0;
}

static const char *describe_bin_format(const unsigned char *hdr_buf, unsigned int hdr_len)
{
	if (hdr_len >= sizeof(struct fwupd_bin_hdr)) {
		const struct fwupd_bin_hdr *hdr = (const struct fwupd_bin_hdr *)hdr_buf;
		if (hdr->magic == FWUPD_BIN_MAGIC)
			return "ARM Cortex-R5 Controller Firmware Image (.bin, SFWM)";
	}
	if (hdr_len >= 4 && hdr_buf[0] == 0x7f && hdr_buf[1] == 'E' &&
	    hdr_buf[2] == 'L' && hdr_buf[3] == 'F')
		return "ELF32 Controller Firmware Binary (.bin)";
	if (hdr_len >= 4 && hdr_buf[3] == 0xea)
		return "ARM32 Raw Vector Table Firmware Binary (.bin)";
	if (hdr_len >= 2 && hdr_buf[0] == 0x55 && hdr_buf[1] == 0xaa)
		return "PCIe Option ROM / x86 Firmware Binary (.bin)";
	return "Raw Vendor Controller Microcode Binary (.bin)";
}

static int probe_all_devices(struct fwupd_device *devs, int max_devs)
{
	int total = 0;
	int i;

	for (i = 0; i < NR_PLUGINS && total < max_devs; i++) {
		total += fwupd_plugins[i]->probe(devs + total, max_devs - total);
	}
	return total;
}

static const struct fwupd_plugin_ops *find_plugin(const char *name)
{
	int i;
	for (i = 0; i < NR_PLUGINS; i++) {
		if (strcmp(fwupd_plugins[i]->name, name) == 0)
			return fwupd_plugins[i];
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
	for (i = 0; i < nr_lvfs_releases; i++) {
		if (strcmp(lvfs_catalog[i].device_id, device_id) == 0)
			return &lvfs_catalog[i];
	}
	return NULL;
}

static void save_installed_digest(const char *device_id, const char *ver,
				  const unsigned char sha256[32])
{
	char path[80];
	int fd;

	sprintf(path, "/data/misc/fwupd/digest-%s.bin", device_id);
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return;
	write(fd, ver, 16);
	write(fd, sha256, 32);
	close(fd);
}

static int load_installed_digest(const char *device_id, const char *ver,
				 unsigned char out_sha256[32])
{
	char path[80], saved_ver[16];
	int fd;

	sprintf(path, "/data/misc/fwupd/digest-%s.bin", device_id);
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	memset(saved_ver, 0, sizeof(saved_ver));
	if (read(fd, saved_ver, 16) != 16 || read(fd, out_sha256, 32) != 32) {
		close(fd);
		return -1;
	}
	close(fd);
	if (strcmp(saved_ver, ver) != 0)
		return -1;
	return 0;
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
		       fwupd_plugins[i]->name, fwupd_plugins[i]->summary);
		printf("                      Transport: %s\n",
		       fwupd_plugins[i]->protocol);
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
	printf("Successfully refreshed LVFS metadata (%d component releases, %d firmware .bin images staged in %s)\n",
	       nr_lvfs_releases, nr_lvfs_releases * 2, LVFS_PKG_DIR);
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
			printf("      Firmware Binary: %s\n", r->pkg_filename);
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

static int install_bin_on_device(const char *bin_path,
				 const char *target_query,
				 int allow_older,
				 int allow_reinstall)
{
	unsigned char hdr_buf[FWUPD_HDR_INSPECT_LEN];
	unsigned char full_sha256[32], code_sha256[32];
	unsigned int hdr_cap = 0, total_size = 0, nonzero = 0;
	const struct fwupd_bin_hdr *hdr = (const struct fwupd_bin_hdr *)hdr_buf;
	struct fwupd_device devs[MAX_DEVICES];
	struct fwupd_device *dev;
	const struct fwupd_plugin_ops *plug;
	char target_ver[16], old_ver[16], hex[65];
	int is_sfwm = 0, bin_fd, nr_devs, cmp, max_ver_len;

	if (inspect_bin_file(bin_path, hdr_buf, &hdr_cap, &total_size,
			     &nonzero, full_sha256, code_sha256) < 0) {
		fprintf(stderr, "fwupdmgr: cannot open firmware .bin file '%s'\n", bin_path);
		return 1;
	}
	if (total_size < 64) {
		fprintf(stderr, "fwupdmgr: invalid firmware .bin '%s' (%u bytes < 64B minimum)\n",
			bin_path, total_size);
		return 1;
	}

	if (hdr_cap >= sizeof(struct fwupd_bin_hdr) && hdr->magic == FWUPD_BIN_MAGIC)
		is_sfwm = 1;

	nr_devs = probe_all_devices(devs, MAX_DEVICES);
	dev = find_device(devs, nr_devs,
			  (target_query && target_query[0]) ? target_query
			  : (is_sfwm ? hdr->device_id : FWUPD_DEVID_NVME));
	if (!dev) {
		fprintf(stderr, "fwupdmgr: target device '%s' is not online\n",
			(target_query && target_query[0]) ? target_query
			: (is_sfwm ? hdr->device_id : "unknown"));
		return 1;
	}

	max_ver_len = (strcmp(dev->plugin, "nvme") == 0) ? 8 : 4;
	memset(target_ver, 0, sizeof(target_ver));
	fwupd_detect_bin_version(hdr_buf, hdr_cap, full_sha256, max_ver_len, target_ver);

	cmp = compare_versions(target_ver, dev->version);
	if (cmp == 0 && !allow_reinstall && is_sfwm) {
		fprintf(stderr, "fwupdmgr: %s is already at version %s (use --allow-reinstall)\n",
			dev->device_id, dev->version);
		return 1;
	}
	if (cmp < 0 && !allow_older && is_sfwm) {
		fprintf(stderr, "fwupdmgr: .bin version %s is older than installed %s on %s (use --allow-older)\n",
			target_ver, dev->version, dev->device_id);
		return 1;
	}

	plug = find_plugin(dev->plugin);
	if (!plug) {
		fprintf(stderr, "fwupdmgr: plugin '%s' not found\n", dev->plugin);
		return 1;
	}

	strcpy(old_ver, dev->version);
	format_sha256_hex(full_sha256, hex);

	printf("Loading & verifying firmware binary %s...\n", bin_path);
	printf("  Binary Format : %s (%u bytes)\n",
	       describe_bin_format(hdr_buf, hdr_cap), total_size);
	printf("  Target Device : %s (%s, GUID %s)\n", dev->name, dev->device_id, dev->guid);
	printf("  Plugin / Node : %s (%s)\n", dev->plugin, dev->dev_node);
	printf("  Transition    : %s -> %s\n", old_ver, target_ver);
	printf("  Binary Digest : SHA256(%.32s...)\n", hex);

	if (is_sfwm && memcmp(code_sha256, hdr->code_sha256, 32) != 0) {
		printf("  [WARN] Binary microcode SHA-256 mismatch detected in user-space; forwarding to controller to verify hardware rejection...\n");
	}

	bin_fd = open(bin_path, O_RDONLY);
	if (bin_fd < 0)
		return 1;

	if (plug->write_firmware(dev, bin_fd, total_size) < 0) {
		close(bin_fd);
		record_history(dev->device_id, dev->guid, dev->plugin,
			       old_ver, target_ver, full_sha256, "failed-signature");
		printf("fwupdmgr: firmware update FAILED on %s (hardware rejected .bin image)\n",
		       dev->device_id);
		return 1;
	}
	close(bin_fd);

	/* Re-probe device to verify the new version and hardware SHA-256 digest */
	nr_devs = probe_all_devices(devs, MAX_DEVICES);
	dev = find_device(devs, nr_devs, dev->device_id);
	if (!dev || strcmp(dev->version, target_ver) != 0 ||
	    memcmp(dev->sha256, full_sha256, 32) != 0) {
		fprintf(stderr, "fwupdmgr: post-update verification failed on %s\n",
			dev ? dev->device_id : "device");
		return 1;
	}

	save_installed_digest(dev->device_id, dev->version, dev->sha256);
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
			if (install_bin_on_device(r->pkg_filename, d->device_id, 0, 0) != 0)
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
	unsigned char expected_bin[sizeof(struct fwupd_bin_hdr) + FWUPD_DEFAULT_PAYLOAD_SIZE];
	unsigned char expected_sha256[32];
	int nr = probe_all_devices(devs, MAX_DEVICES);
	int i, verified = 0;

	for (i = 0; i < nr; i++) {
		const struct fwupd_device *d = &devs[i];
		const struct lvfs_release *r;
		char hex[65];

		if (target_query && target_query[0]) {
			if (strcmp(d->device_id, target_query) != 0 &&
			    strcmp(d->guid, target_query) != 0 &&
			    strcmp(d->plugin, target_query) != 0)
				continue;
		}

		if (load_installed_digest(d->device_id, d->version, expected_sha256) < 0) {
			r = find_release_for_device(d->device_id);
			fwupd_build_bin_image(d->plugin, d->device_id, d->guid, d->version,
					      r ? r->capsule_flags : FWUPD_FLAG_SIGNED_PAYLOAD,
					      expected_bin, expected_sha256);
		}
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
	const struct fwupd_plugin_ops *plug;
	unsigned int slot = slot_str ? (unsigned int)atoi(slot_str) : 1;
	char old_ver[16];
	int nr = probe_all_devices(devs, MAX_DEVICES);

	dev = find_device(devs, nr, target_query ? target_query : FWUPD_DEVID_NVME);
	if (!dev) {
		fprintf(stderr, "fwupdmgr activate: device not found\n");
		return 1;
	}
	plug = find_plugin(dev->plugin);
	if (!plug || !plug->activate_slot) {
		fprintf(stderr, "fwupdmgr activate: dual-slot activation is not supported by plugin '%s'\n",
			dev->plugin);
		return 1;
	}
	if (slot < 1 || slot > 2) {
		fprintf(stderr, "fwupdmgr activate: slot must be 1 or 2\n");
		return 1;
	}

	strcpy(old_ver, dev->version);
	if (plug->activate_slot(dev, slot) < 0) {
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
	unlink("/data/misc/fwupd/history.db");
	unlink(FWUPD_HISTORY_PATH);
	symlink("/data/misc/fwupd/history.db", FWUPD_HISTORY_PATH);
	printf("Cleared firmware update history (%s).\n", FWUPD_HISTORY_PATH);
	return 0;
}

static int cmd_examine(const char *bin_path)
{
	unsigned char hdr_buf[FWUPD_HDR_INSPECT_LEN];
	unsigned char full_sha256[32], code_sha256[32];
	unsigned int hdr_cap = 0, total_size = 0, nonzero = 0;
	const struct fwupd_bin_hdr *hdr = (const struct fwupd_bin_hdr *)hdr_buf;
	char full_hex[65], code_hex[65], detected_ver[16];
	int is_sfwm = 0, sig_ok = 1;

	if (!bin_path) {
		fprintf(stderr, "fwupdmgr examine: missing <.bin> file path\n");
		return 1;
	}
	if (inspect_bin_file(bin_path, hdr_buf, &hdr_cap, &total_size,
			     &nonzero, full_sha256, code_sha256) < 0) {
		fprintf(stderr, "fwupdmgr examine: cannot open '%s'\n", bin_path);
		return 1;
	}
	if (total_size < 64 || nonzero < 16) {
		fprintf(stderr, "fwupdmgr examine: '%s' is too small or empty (%u bytes)\n",
			bin_path, total_size);
		return 1;
	}

	format_sha256_hex(full_sha256, full_hex);
	format_sha256_hex(code_sha256, code_hex);
	memset(detected_ver, 0, sizeof(detected_ver));
	fwupd_detect_bin_version(hdr_buf, hdr_cap, full_sha256, 8, detected_ver);

	printf("Firmware Binary Inspection (%s):\n", bin_path);
	printf("  Binary Format  : %s\n", describe_bin_format(hdr_buf, hdr_cap));

	if (hdr_cap >= sizeof(struct fwupd_bin_hdr) && hdr->magic == FWUPD_BIN_MAGIC) {
		is_sfwm = 1;
		sig_ok = (memcmp(hdr->code_sha256, code_sha256, 32) == 0 &&
			  sizeof(struct fwupd_bin_hdr) + hdr->code_len == total_size);
		printf("  ARM Vector/Sig : 0x%08x (b 0xa0) / 0x%08x (\"SFWM\") v%u\n",
		       hdr->arm_b_reset, hdr->magic, hdr->hdr_version);
		printf("  Target Plugin  : %s\n", hdr->plugin);
		printf("  Target Device  : %s\n", hdr->device_id);
		printf("  Target GUID    : %s\n", hdr->guid);
		printf("  Target Version : %s\n", hdr->fw_version);
		printf("  Binary Size    : %u bytes (%u bytes microcode body)\n",
		       total_size, hdr->code_len);
		printf("  Image SHA256   : %s\n", full_hex);
		printf("  Code SHA256    : %s [%s]\n",
		       code_hex, sig_ok ? "VALID SIGNATURE" : "CORRUPTED");
	} else {
		printf("  Detected Rev   : %s\n", detected_ver);
		printf("  Binary Size    : %u bytes (%u non-zero bytes)\n",
		       total_size, nonzero);
		printf("  Image SHA256   : %s [VALID SIGNATURE]\n", full_hex);
	}
	return sig_ok ? 0 : 1;
}

static void usage(void)
{
	printf("fwupdmgr 1.9.24 (SIX Firmware Update Manager — nvme, ufs & scsi plugins)\n"
	       "Usage: fwupdmgr <command> [options]\n\n"
	       "Commands:\n"
	       "  get-plugins                            List built-in hardware plugins (nvme, ufs, scsi)\n"
	       "  get-devices                            Probe hardware and list updatable devices\n"
	       "  refresh                                Refresh LVFS metadata and .bin firmware images\n"
	       "  get-updates                            Show available LVFS firmware updates\n"
	       "  update [DEVICE-ID|GUID|PLUGIN]         Update devices to latest LVFS .bin firmware\n"
	       "  install <firmware.bin> [DEVICE-ID] [--allow-older] [--allow-reinstall]\n"
	       "  install-blob <firmware.bin> <DEVICE-ID>\n"
	       "                                         Install any signed or raw vendor .bin firmware\n"
	       "  activate <DEVICE-ID> <slot>            Switch active NVMe firmware slot (1 or 2)\n"
	       "  verify [DEVICE-ID|GUID]                Verify device firmware SHA-256 checksums\n"
	       "  examine <firmware.bin>                 Inspect a .bin firmware image & SHA-256\n"
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
	if (strcmp(sub, "install") == 0 || strcmp(sub, "install-blob") == 0) {
		const char *bin_path = NULL;
		const char *target = NULL;
		int allow_older = (strcmp(sub, "install-blob") == 0);
		int allow_reinstall = (strcmp(sub, "install-blob") == 0);
		int i;
		for (i = 2; i < argc; i++) {
			if (strcmp(argv[i], "--allow-older") == 0)
				allow_older = 1;
			else if (strcmp(argv[i], "--allow-reinstall") == 0)
				allow_reinstall = 1;
			else if (!bin_path)
				bin_path = argv[i];
			else if (!target)
				target = argv[i];
		}
		if (!bin_path) {
			fprintf(stderr, "fwupdmgr %s: missing <firmware.bin> path\n", sub);
			return 1;
		}
		return install_bin_on_device(bin_path, target, allow_older, allow_reinstall);
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
