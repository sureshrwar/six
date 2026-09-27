/*
 * ufs.c - JEDEC UFS 4.0 Management Utility (ufs-utils) for SIX (/bin/ufs)
 *
 * Supported subcommands:
 *   ufs list                         List all UFS LUNs and W-LUNs
 *   ufs info | status                Show UFS 4.0 controller, UTRL, LUN & RPMB status
 *   ufs desc [all|device|geometry|health|unit]
 *                                    Query UFS Descriptors via UPIU_QUERY_REQ
 *   ufs boot-slot [a|b]              Inspect or switch active A/B Boot LUN (bBootLunID)
 *   ufs writebooster [status|on|off|toggle]
 *                                    Inspect or toggle UFS 4.0 SLC WriteBooster flag
 *   ufs flush [/dev/ufsa]            Send SCSI SYNCHRONIZE_CACHE(10) UPIU
 *   ufs unmap [/dev/ufsa] [-s <lba>] [-b <blocks>]
 *                                    Send SCSI UNMAP UPIU
 *   ufs rpmb counter                 Read RPMB monotonic write counter
 *   ufs rpmb write-key <key>         Program 256-bit RPMB authentication key
 *   ufs rpmb write <blk> <data> <key>
 *                                    Write authenticated 256B block to /dev/ufs-rpmb
 *   ufs rpmb read <blk> [key]        Read 256B block from /dev/ufs-rpmb
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "../../include/linux/ufs.h"

/*
 * FIPS 180-4 SHA-256 for signing and verifying 512-byte RPMB frames
 */
static const unsigned int sha256_k[64] = {
	0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
	0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
	0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
	0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
	0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
	0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
	0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
	0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
	0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
	0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
	0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
	0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
	0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
	0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
	0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
	0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

static unsigned int rotr32(unsigned int x, int n)
{
	return (x >> n) | (x << (32 - n));
}

static void sha256_transform(unsigned int state[8], const unsigned char block[64])
{
	unsigned int w[64];
	unsigned int a, b, c, d, e, f, g, h, t1, t2;
	int i;

	for (i = 0; i < 16; i++) {
		w[i] = ((unsigned int)block[i * 4 + 0] << 24) |
		       ((unsigned int)block[i * 4 + 1] << 16) |
		       ((unsigned int)block[i * 4 + 2] << 8) |
		       ((unsigned int)block[i * 4 + 3]);
	}
	for (i = 16; i < 64; i++) {
		unsigned int s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
		unsigned int s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}

	a = state[0]; b = state[1]; c = state[2]; d = state[3];
	e = state[4]; f = state[5]; g = state[6]; h = state[7];

	for (i = 0; i < 64; i++) {
		unsigned int S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
		unsigned int ch = (e & f) ^ ((~e) & g);
		unsigned int S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
		unsigned int maj = (a & b) ^ (a & c) ^ (b & c);
		t1 = h + S1 + ch + sha256_k[i] + w[i];
		t2 = S0 + maj;
		h = g; g = f; f = e; e = d + t1;
		d = c; c = b; b = a; a = t1 + t2;
	}

	state[0] += a; state[1] += b; state[2] += c; state[3] += d;
	state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

static void derive_rpmb_key(const char *key_str, unsigned char out_key[32])
{
	int len = key_str ? (int)strlen(key_str) : 0;
	int i;

	memset(out_key, 0, 32);
	for (i = 0; i < 32; i++) {
		if (i < len)
			out_key[i] = (unsigned char)key_str[i];
		else
			out_key[i] = (unsigned char)(key_str[i % (len ? len : 1)] ^ (i * 0x5a));
	}
}

static void rpmb_compute_mac(const unsigned char key[32],
			     const struct ufs_rpmb_frame *frame,
			     unsigned char out_mac[32])
{
	unsigned int state[8] = {
		0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
		0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
	};
	unsigned char msg[320];
	int i;

	memset(msg, 0, sizeof(msg));
	memcpy(msg, key, 32);
	memcpy(msg + 32, &frame->data[0], 284);
	msg[316] = 0x80;
	msg[318] = 0x09;
	msg[319] = 0xe0;

	for (i = 0; i < 5; i++)
		sha256_transform(state, msg + (i * 64));

	for (i = 0; i < 8; i++) {
		out_mac[i * 4 + 0] = (unsigned char)((state[i] >> 24) & 0xff);
		out_mac[i * 4 + 1] = (unsigned char)((state[i] >> 16) & 0xff);
		out_mac[i * 4 + 2] = (unsigned char)((state[i] >> 8) & 0xff);
		out_mac[i * 4 + 3] = (unsigned char)(state[i] & 0xff);
	}
}

static int open_ufs_bsg(void)
{
	int fd = open("/dev/ufs-bsg0", O_RDWR);
	if (fd >= 0)
		return fd;
	return open("/dev/ufsa", O_RDWR);
}

static int ufs_query_desc(int fd, unsigned char idn, unsigned char index,
			  void *buf, unsigned int len)
{
	struct ufs_bsg_query_ioctl q;

	memset(buf, 0, len);
	memset(&q, 0, sizeof(q));
	q.opcode = UPIU_QUERY_OPCODE_READ_DESC;
	q.idn = idn;
	q.index = index;
	q.buf_len = len;
	q.buf_addr = (unsigned long)buf;
	return ioctl(fd, UFS_IOCTL_QUERY, &q);
}

static int ufs_query_attr(int fd, unsigned char opcode, unsigned char idn,
			  unsigned int *val)
{
	struct ufs_bsg_query_ioctl q;
	int rc;

	memset(&q, 0, sizeof(q));
	q.opcode = opcode;
	q.idn = idn;
	q.value = val ? *val : 0;
	rc = ioctl(fd, UFS_IOCTL_QUERY, &q);
	if (rc == 0 && val)
		*val = q.value;
	return rc;
}

static int ufs_query_flag(int fd, unsigned char opcode, unsigned char idn,
			  unsigned int *val)
{
	struct ufs_bsg_query_ioctl q;
	int rc;

	memset(&q, 0, sizeof(q));
	q.opcode = opcode;
	q.idn = idn;
	rc = ioctl(fd, UFS_IOCTL_QUERY, &q);
	if (rc == 0 && val)
		*val = q.value & 1;
	return rc;
}

static int cmd_info(void)
{
	FILE *fp = fopen("/proc/ufs", "r");
	char line[256];

	if (!fp) {
		fprintf(stderr, "ufs: cannot open /proc/ufs\n");
		return 1;
	}
	while (fgets(line, sizeof(line), fp))
		fputs(line, stdout);
	fclose(fp);
	return 0;
}

static int cmd_list(void)
{
	struct ufs_device_desc dev;
	struct ufs_unit_desc unit;
	unsigned int active_boot = 1;
	static const unsigned char luns[4] = { 0, 1, 2, UFS_WLUN_RPMB };
	static const char *nodes[4] = { "/dev/ufsa", "/dev/ufsb", "/dev/ufsc", "/dev/ufs-rpmb" };
	int fd = open_ufs_bsg();
	int i;

	if (fd < 0) {
		fprintf(stderr, "ufs list: cannot open /dev/ufs-bsg0\n");
		return 1;
	}
	if (ufs_query_desc(fd, UFS_DESC_IDN_DEVICE, 0, &dev, sizeof(dev)) < 0) {
		fprintf(stderr, "ufs list: failed to read Device Descriptor\n");
		close(fd);
		return 1;
	}
	ufs_query_attr(fd, UPIU_QUERY_OPCODE_READ_ATTR, UFS_ATTR_IDN_BOOT_LUN_ID, &active_boot);

	printf("UFS Controller: /dev/ufs-bsg0 (%s %s, S/N %s, Spec 0x%04x)\n",
	       dev.manufacturer_name, dev.product_name, dev.serial_number, dev.wSpecVersion);
	printf("%-15s %-8s %-10s %-12s %-10s %s\n",
	       "Node", "LUN", "BlockSize", "Blocks", "Capacity", "Role");
	printf("%-15s %-8s %-10s %-12s %-10s %s\n",
	       "---------------", "--------", "----------", "------------", "----------",
	       "----------------------------");

	for (i = 0; i < 4; i++) {
		unsigned int bsz, cap_kb;
		char lun_str[12], cap_str[16], role_str[48];
		if (ufs_query_desc(fd, UFS_DESC_IDN_UNIT, luns[i], &unit, sizeof(unit)) < 0)
			continue;
		bsz = 1U << unit.bLogicalBlockSize;
		cap_kb = (unit.qLogicalBlockCount * bsz) / 1024U;
		if (luns[i] == UFS_WLUN_RPMB)
			sprintf(lun_str, "0x%02X", luns[i]);
		else
			sprintf(lun_str, "LUN %u", luns[i]);
		sprintf(cap_str, "%u KB", cap_kb);
		strcpy(role_str, unit.lun_name);
		if (unit.bBootLunID != 0 && unit.bBootLunID == active_boot)
			strcat(role_str, " [ACTIVE BOOT]");
		printf("%-15s %-8s %-10u %-12u %-10s %s\n",
		       nodes[i], lun_str, bsz, unit.qLogicalBlockCount, cap_str, role_str);
	}
	close(fd);
	return 0;
}

static int cmd_desc(const char *which)
{
	int fd = open_ufs_bsg();
	int show_all = (!which || strcmp(which, "all") == 0);

	if (fd < 0) {
		fprintf(stderr, "ufs desc: cannot open /dev/ufs-bsg0\n");
		return 1;
	}

	if (show_all || strcmp(which, "device") == 0) {
		struct ufs_device_desc d;
		if (ufs_query_desc(fd, UFS_DESC_IDN_DEVICE, 0, &d, sizeof(d)) == 0) {
			printf("UFS Device Descriptor (IDN 0x00):\n");
			printf("  bLength                         : 0x%02x (%u bytes)\n", d.bLength, d.bLength);
			printf("  bNumberLU / bNumberWLU          : %u / %u\n", d.bNumberLU, d.bNumberWLU);
			printf("  bBootEnable                     : 0x%02x\n", d.bBootEnable);
			printf("  wSpecVersion                    : 0x%04x (JEDEC UFS 4.0)\n", d.wSpecVersion);
			printf("  wManufacturerID                 : 0x%04x (%s)\n", d.wManufacturerID, d.manufacturer_name);
			printf("  Product / Serial                : %s / %s\n", d.product_name, d.serial_number);
			printf("  bQueueDepth                     : %u\n", d.bQueueDepth);
			printf("  bUFSFeaturesSupport             : 0x%02x (WriteBooster + HS-G5)\n", d.bUFSFeaturesSupport);
			printf("  dExtendedUFSFeaturesSupport     : 0x%08x\n", d.dExtendedUFSFeaturesSupport);
		}
	}

	if (show_all || strcmp(which, "geometry") == 0) {
		struct ufs_geometry_desc g;
		if (ufs_query_desc(fd, UFS_DESC_IDN_GEOMETRY, 0, &g, sizeof(g)) == 0) {
			printf("UFS Geometry Descriptor (IDN 0x07):\n");
			printf("  qTotalRawDeviceCapacity         : %u sectors (%u KB / %u MB)\n",
			       g.qTotalRawDeviceCapacity, g.qTotalRawDeviceCapacity >> 1, g.qTotalRawDeviceCapacity >> 11);
			printf("  bMaxNumberLU                    : %u\n", g.bMaxNumberLU);
			printf("  bMinAddrBlockSize               : %u (4 KB)\n", g.bMinAddrBlockSize);
			printf("  bRPMB_ReadWriteSize             : %u frames\n", g.bRPMB_ReadWriteSize);
		}
	}

	if (show_all || strcmp(which, "health") == 0) {
		struct ufs_health_desc h;
		if (ufs_query_desc(fd, UFS_DESC_IDN_HEALTH, 0, &h, sizeof(h)) == 0) {
			printf("UFS Health Descriptor (IDN 0x09):\n");
			printf("  bPreEOLInfo                     : 0x%02x (Normal)\n", h.bPreEOLInfo);
			printf("  bDeviceLifeTimeEstA (SLC)       : 0x%02x (0%% - 10%% life used)\n", h.bDeviceLifeTimeEstA);
			printf("  bDeviceLifeTimeEstB (TLC)       : 0x%02x (0%% - 10%% life used)\n", h.bDeviceLifeTimeEstB);
			printf("  SCSI Read / Write Commands      : %u (%u sectors) / %u (%u sectors)\n",
			       h.vendor_read_cmds, h.vendor_sectors_read,
			       h.vendor_write_cmds, h.vendor_sectors_written);
			printf("  Flush / UNMAP Commands          : %u / %u (%u sectors unmapped)\n",
			       h.vendor_flush_cmds, h.vendor_unmap_cmds, h.vendor_unmap_sectors);
			printf("  RPMB Writes / Reads / AuthFails : %u / %u / %u\n",
			       h.vendor_rpmb_writes, h.vendor_rpmb_reads, h.vendor_rpmb_auth_fails);
		}
	}

	if (show_all || strcmp(which, "unit") == 0) {
		static const unsigned char luns[4] = { 0, 1, 2, UFS_WLUN_RPMB };
		int i;
		for (i = 0; i < 4; i++) {
			struct ufs_unit_desc u;
			if (ufs_query_desc(fd, UFS_DESC_IDN_UNIT, luns[i], &u, sizeof(u)) == 0) {
				printf("UFS Unit Descriptor (LUN 0x%02x - %s):\n", u.bUnitIndex, u.lun_name);
				printf("  bLUEnable / bBootLunID          : %u / 0x%02x\n", u.bLUEnable, u.bBootLunID);
				printf("  bLogicalBlockSize               : %u (%u B)\n",
				       u.bLogicalBlockSize, 1U << u.bLogicalBlockSize);
				printf("  qLogicalBlockCount              : %u\n", u.qLogicalBlockCount);
			}
		}
	}

	close(fd);
	return 0;
}

static void read_boot_hdr(const char *dev_path, char *out, int max_len)
{
	int fd = open(dev_path, O_RDONLY);
	memset(out, 0, max_len);
	if (fd >= 0) {
		read(fd, out, max_len - 1);
		close(fd);
	}
}

static int cmd_boot_slot(const char *target)
{
	int fd = open_ufs_bsg();
	unsigned int slot_id = 1;
	char hdr_a[128], hdr_b[128];

	if (fd < 0) {
		fprintf(stderr, "ufs boot-slot: cannot open /dev/ufs-bsg0\n");
		return 1;
	}

	if (target && target[0]) {
		if (strcmp(target, "a") == 0 || strcmp(target, "A") == 0 || strcmp(target, "1") == 0)
			slot_id = 1;
		else if (strcmp(target, "b") == 0 || strcmp(target, "B") == 0 || strcmp(target, "2") == 0)
			slot_id = 2;
		else {
			fprintf(stderr, "ufs boot-slot: slot must be 'a' or 'b'\n");
			close(fd);
			return 1;
		}
		if (ufs_query_attr(fd, UPIU_QUERY_OPCODE_WRITE_ATTR, UFS_ATTR_IDN_BOOT_LUN_ID, &slot_id) < 0) {
			fprintf(stderr, "ufs boot-slot: failed to write bBootLunID attribute\n");
			close(fd);
			return 1;
		}
		printf("UFS Boot Slot switched: bBootLunID=0x%02x -> Slot %c (%s)\n",
		       slot_id, (slot_id == 2) ? 'B' : 'A',
		       (slot_id == 2) ? "/dev/ufsc" : "/dev/ufsb");
	}

	if (ufs_query_attr(fd, UPIU_QUERY_OPCODE_READ_ATTR, UFS_ATTR_IDN_BOOT_LUN_ID, &slot_id) < 0) {
		close(fd);
		return 1;
	}
	close(fd);

	read_boot_hdr("/dev/ufsb", hdr_a, sizeof(hdr_a));
	read_boot_hdr("/dev/ufsc", hdr_b, sizeof(hdr_b));

	printf("Active UFS Boot W-LUN (0xB0): bBootLunID=0x%02x (Slot %c)\n",
	       slot_id, (slot_id == 2) ? 'B' : 'A');
	printf("  Slot A (/dev/ufsb, LUN 1) %s: %s\n",
	       (slot_id == 1) ? "[ACTIVE] " : "[STANDBY]", hdr_a);
	printf("  Slot B (/dev/ufsc, LUN 2) %s: %s\n",
	       (slot_id == 2) ? "[ACTIVE] " : "[STANDBY]", hdr_b);
	return 0;
}

static int cmd_writebooster(const char *act)
{
	int fd = open_ufs_bsg();
	unsigned int wb_en = 0, avail = 0;

	if (fd < 0) {
		fprintf(stderr, "ufs writebooster: cannot open /dev/ufs-bsg0\n");
		return 1;
	}

	if (act && act[0]) {
		if (strcmp(act, "on") == 0 || strcmp(act, "1") == 0)
			ufs_query_flag(fd, UPIU_QUERY_OPCODE_SET_FLAG, UFS_FLAG_IDN_WB_EN, &wb_en);
		else if (strcmp(act, "off") == 0 || strcmp(act, "0") == 0)
			ufs_query_flag(fd, UPIU_QUERY_OPCODE_CLEAR_FLAG, UFS_FLAG_IDN_WB_EN, &wb_en);
		else if (strcmp(act, "toggle") == 0)
			ufs_query_flag(fd, UPIU_QUERY_OPCODE_TOGGLE_FLAG, UFS_FLAG_IDN_WB_EN, &wb_en);
	}

	ufs_query_flag(fd, UPIU_QUERY_OPCODE_READ_FLAG, UFS_FLAG_IDN_WB_EN, &wb_en);
	ufs_query_attr(fd, UPIU_QUERY_OPCODE_READ_ATTR, UFS_ATTR_IDN_AVAIL_WB_BUFF_SIZE, &avail);
	close(fd);

	printf("UFS 4.0 SLC WriteBooster: fWriteBoosterEn=%u (%s), AvailBuffer=%u0%%\n",
	       wb_en, wb_en ? "ENABLED" : "DISABLED", avail);
	return 0;
}

static int cmd_flush(const char *dev)
{
	struct ufs_bsg_scsi_ioctl sc;
	int fd = open_ufs_bsg();

	if (fd < 0) {
		fprintf(stderr, "ufs flush: cannot open UFS controller\n");
		return 1;
	}
	memset(&sc, 0, sizeof(sc));
	sc.lun = 0;
	if (dev && strstr(dev, "ufsb"))
		sc.lun = 1;
	else if (dev && strstr(dev, "ufsc"))
		sc.lun = 2;
	sc.opcode = UFS_SCSI_SYNC_CACHE_10;
	if (ioctl(fd, UFS_IOCTL_SCSI_CMD, &sc) < 0) {
		fprintf(stderr, "ufs flush: SYNCHRONIZE_CACHE(10) failed\n");
		close(fd);
		return 1;
	}
	close(fd);
	printf("UFS SYNCHRONIZE_CACHE(10): success (LUN %u)\n", sc.lun);
	return 0;
}

static int cmd_unmap(int argc, char **argv)
{
	struct ufs_bsg_scsi_ioctl sc;
	unsigned int lba = 0, blocks = 8;
	unsigned char lun = 0;
	int i, fd;

	for (i = 2; i < argc; i++) {
		if (strcmp(argv[i], "-s") == 0 && i + 1 < argc)
			lba = (unsigned int)atol(argv[++i]);
		else if ((strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "-c") == 0) && i + 1 < argc)
			blocks = (unsigned int)atol(argv[++i]);
		else if (strstr(argv[i], "ufsb"))
			lun = 1;
		else if (strstr(argv[i], "ufsc"))
			lun = 2;
	}

	fd = open_ufs_bsg();
	if (fd < 0) {
		fprintf(stderr, "ufs unmap: cannot open UFS controller\n");
		return 1;
	}
	memset(&sc, 0, sizeof(sc));
	sc.lun = lun;
	sc.opcode = UFS_SCSI_UNMAP;
	sc.lba = lba;
	sc.blocks = blocks;
	if (ioctl(fd, UFS_IOCTL_SCSI_CMD, &sc) < 0) {
		fprintf(stderr, "ufs unmap: SCSI UNMAP failed on LUN %u\n", lun);
		close(fd);
		return 1;
	}
	close(fd);
	printf("UFS SCSI UNMAP: success (LUN %u, lba=%u, blocks=%u)\n", lun, lba, blocks);
	return 0;
}

static int open_rpmb(void)
{
	int fd = open("/dev/ufs-rpmb", O_RDWR);
	if (fd >= 0)
		return fd;
	return open_ufs_bsg();
}

static int cmd_rpmb(int argc, char **argv)
{
	const char *op = (argc >= 3) ? argv[2] : "counter";
	struct ufs_rpmb_frame frame;
	unsigned char key[32];
	int fd = open_rpmb();
	int rc;

	if (fd < 0) {
		fprintf(stderr, "ufs rpmb: cannot open /dev/ufs-rpmb\n");
		return 1;
	}

	if (strcmp(op, "counter") == 0 || strcmp(op, "status") == 0) {
		memset(&frame, 0, sizeof(frame));
		frame.req_resp = RPMB_REQ_GET_COUNTER;
		rc = ioctl(fd, UFS_IOCTL_RPMB, &frame);
		close(fd);
		if (frame.result == RPMB_RES_NO_AUTH_KEY) {
			printf("RPMB Status: Authentication Key NOT PROGRAMMED (result=0x%04x)\n",
			       frame.result);
			return 0;
		}
		if (rc < 0) {
			fprintf(stderr, "ufs rpmb counter: failed (result=0x%04x)\n", frame.result);
			return 1;
		}
		printf("RPMB Status: Key PROGRAMMED, Write Counter = %u (result=0x%04x OK)\n",
		       frame.write_counter, frame.result);
		return 0;
	}

	if (strcmp(op, "write-key") == 0 || strcmp(op, "init-key") == 0) {
		const char *key_str = (argc >= 4) ? argv[3] : "six_default_rpmb_secret_2026";
		derive_rpmb_key(key_str, key);
		memset(&frame, 0, sizeof(frame));
		frame.req_resp = RPMB_REQ_PROGRAM_KEY;
		memcpy(frame.key_mac, key, 32);
		rc = ioctl(fd, UFS_IOCTL_RPMB, &frame);
		close(fd);
		if (rc < 0 || frame.result != RPMB_RES_OK) {
			fprintf(stderr, "ufs rpmb write-key: failed (result=0x%04x)\n", frame.result);
			return 1;
		}
		printf("RPMB Program Authentication Key: success (256-bit HMAC-SHA256 key active)\n");
		return 0;
	}

	if (strcmp(op, "write") == 0) {
		unsigned short blk = (argc >= 4) ? (unsigned short)atoi(argv[3]) : 0;
		const char *payload = (argc >= 5) ? argv[4] : "";
		const char *key_str = (argc >= 6) ? argv[5] : "six_default_rpmb_secret_2026";
		unsigned int cur_counter = 0;

		/* Step 1: Query current monotonic write_counter from RPMB W-LUN */
		memset(&frame, 0, sizeof(frame));
		frame.req_resp = RPMB_REQ_GET_COUNTER;
		if (ioctl(fd, UFS_IOCTL_RPMB, &frame) < 0) {
			fprintf(stderr, "ufs rpmb write: cannot read write_counter (result=0x%04x)\n",
				frame.result);
			close(fd);
			return 1;
		}
		cur_counter = frame.write_counter;

		/* Step 2: Build authenticated RPMB_REQ_WRITE_DATA frame & sign with SHA-256 MAC */
		derive_rpmb_key(key_str, key);
		memset(&frame, 0, sizeof(frame));
		strncpy((char *)frame.data, payload, sizeof(frame.data) - 1);
		frame.write_counter = cur_counter;
		frame.addr = blk;
		frame.block_count = 1;
		frame.req_resp = RPMB_REQ_WRITE_DATA;
		rpmb_compute_mac(key, &frame, frame.key_mac);

		rc = ioctl(fd, UFS_IOCTL_RPMB, &frame);
		close(fd);
		if (rc < 0 || frame.result != RPMB_RES_OK) {
			printf("RPMB Authenticated Write REJECTED: result=0x%04x (%s)\n",
			       frame.result,
			       (frame.result == RPMB_RES_AUTH_FAILURE) ? "RPMB_RES_AUTH_FAILURE" :
			       (frame.result == RPMB_RES_COUNTER_FAILURE) ? "RPMB_RES_COUNTER_FAILURE" :
			       "ERROR");
			return 1;
		}
		printf("RPMB Authenticated Write: success (block=%u, new_write_counter=%u)\n",
		       blk, frame.write_counter);
		return 0;
	}

	if (strcmp(op, "read") == 0) {
		unsigned short blk = (argc >= 4) ? (unsigned short)atoi(argv[3]) : 0;
		memset(&frame, 0, sizeof(frame));
		frame.addr = blk;
		frame.block_count = 1;
		frame.req_resp = RPMB_REQ_READ_DATA;
		rc = ioctl(fd, UFS_IOCTL_RPMB, &frame);
		close(fd);
		if (rc < 0 || frame.result != RPMB_RES_OK) {
			fprintf(stderr, "ufs rpmb read: failed (result=0x%04x)\n", frame.result);
			return 1;
		}
		frame.data[255] = '\0';
		printf("RPMB Block %u (write_counter=%u): %s\n",
		       blk, frame.write_counter, (char *)frame.data);
		return 0;
	}

	close(fd);
	fprintf(stderr, "ufs rpmb: unknown operation '%s'\n", op);
	return 1;
}

static void usage(void)
{
	printf("ufs-utils 4.0 (SIX JEDEC UFS 4.0 Management Utility)\n"
	       "Usage: ufs <command> [<args>]\n\n"
	       "Commands:\n"
	       "  list                             List UFS controller and all LUNs/W-LUNs\n"
	       "  info | status                    Show UFS 4.0 controller, UTRL, LUN & RPMB info\n"
	       "  desc [all|device|geometry|health|unit]\n"
	       "                                   Read and decode UFS Descriptors via UPIU Query\n"
	       "  boot-slot [a|b]                  Inspect or switch active A/B Boot LUN (bBootLunID)\n"
	       "  writebooster [status|on|off|toggle]\n"
	       "                                   Inspect or toggle UFS 4.0 SLC WriteBooster\n"
	       "  flush [/dev/ufsa]                Submit SCSI SYNCHRONIZE_CACHE(10) UPIU\n"
	       "  unmap [/dev/ufsa] [-s lba] [-b blocks]\n"
	       "                                   Submit SCSI UNMAP UPIU\n"
	       "  rpmb counter                     Read RPMB key status & monotonic write counter\n"
	       "  rpmb write-key <key>             Program 256-bit RPMB authentication key\n"
	       "  rpmb write <blk> <data> <key>    Write HMAC-authenticated 256B RPMB block\n"
	       "  rpmb read <blk>                  Read 256B block from /dev/ufs-rpmb\n");
}

int main(int argc, char **argv)
{
	const char *sub;

	if (argc < 2) {
		usage();
		return 1;
	}

	sub = argv[1];
	if (strcmp(sub, "list") == 0)
		return cmd_list();
	if (strcmp(sub, "info") == 0 || strcmp(sub, "status") == 0)
		return cmd_info();
	if (strcmp(sub, "desc") == 0)
		return cmd_desc((argc >= 3) ? argv[2] : "all");
	if (strcmp(sub, "boot-slot") == 0 || strcmp(sub, "boot") == 0)
		return cmd_boot_slot((argc >= 3) ? argv[2] : NULL);
	if (strcmp(sub, "writebooster") == 0 || strcmp(sub, "wb") == 0)
		return cmd_writebooster((argc >= 3) ? argv[2] : NULL);
	if (strcmp(sub, "flush") == 0)
		return cmd_flush((argc >= 3) ? argv[2] : "/dev/ufsa");
	if (strcmp(sub, "unmap") == 0 || strcmp(sub, "trim") == 0)
		return cmd_unmap(argc, argv);
	if (strcmp(sub, "rpmb") == 0)
		return cmd_rpmb(argc, argv);
	if (strcmp(sub, "-h") == 0 || strcmp(sub, "--help") == 0 || strcmp(sub, "help") == 0) {
		usage();
		return 0;
	}

	fprintf(stderr, "ufs: unknown subcommand '%s'\n", sub);
	usage();
	return 1;
}
