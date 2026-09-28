/*
 *  linux/drivers/block/ufshcd.c
 *
 *  JEDEC Universal Flash Storage (UFS 4.0 / UFSHCI 4.0) Host Controller &
 *  Multi-LUN / RPMB Driver for SIX.
 *
 *  Exposes:
 *    - /dev/ufs-bsg0 (char 57, minor 0): UFS BSG Controller (UPIU Query / SCSI)
 *    - /dev/ufs-rpmb (char 57, minor 1): UFS W-LUN RPMB (0xC4) Security Node
 *    - /dev/ufsa     (block 58, minor 0): LUN 0 (4 MB Data LUN, mounted at /ufs)
 *    - /dev/ufsb     (block 58, minor 1): LUN 1 (384 KB Boot LUN A, slot_a)
 *    - /dev/ufsc     (block 58, minor 2): LUN 2 (384 KB Boot LUN B, slot_b)
 *    - /proc/ufs     (PROC_UFS): Controller registers, UTRL ring, LUNs & RPMB
 *
 *  All LUNs, persistent attributes/flags (bBootLunID, fWriteBoosterEn), and
 *  the 512-block hardware-authenticated RPMB region live inside a single
 *  unified 5 MB host flash image: ./disk/x86/ufs0.img.
 */

#define MAJOR_NR UFS_MAJOR

#include <solaris.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/major.h>
#include <linux/genhd.h>
#include <linux/string.h>
#include <linux/blk.h>
#include <linux/ufs.h>
#include <linux/fwupd.h>
#include <asm/segment.h>
#include <asm/system.h>

#define UFS_MAX_MINORS		4
#define UFS_HDR_MAGIC		0x55465335U	/* "UFS5" (.bin full SHA-256) */

struct ufs_persist_hdr {
	unsigned int	magic;
	unsigned char	boot_lun_id;		/* 0x01 = Boot A (ufsb), 0x02 = Boot B (ufsc) */
	unsigned char	write_booster_en;	/* 1 = enabled, 0 = disabled */
	unsigned char	boot_lun_en;		/* 1 = enabled */
	unsigned char	active_icc_level;	/* 0x0f */
	unsigned int	power_cycles;
	unsigned int	rpmb_key_set;
	unsigned int	rpmb_write_counter;
	unsigned char	rpmb_key[32];
	char		fw_rev[8];
	unsigned short	device_version;
	unsigned short	ffu_count;
	unsigned char	fw_sha256[32];
	unsigned char	rsvd[412];
};

struct ufs_lun_geom {
	const char	*dev_name;
	const char	*role;
	unsigned long	byte_offset;
	unsigned long	nr_sectors;
	unsigned char	boot_lun_id;
	unsigned char	memory_type;
};

static const struct ufs_lun_geom ufs_luns[UFS_MAX_LUNS] = {
	{ "ufsa", "LUN 0 (Data /ufs)",    UFS_IMG_LUN0_OFFSET,  UFS_IMG_LUN0_SECTORS,  0x00, 0x00 },
	{ "ufsb", "LUN 1 (Boot Slot A)",  UFS_IMG_BOOTA_OFFSET, UFS_IMG_BOOTA_SECTORS, 0x01, 0x01 },
	{ "ufsc", "LUN 2 (Boot Slot B)",  UFS_IMG_BOOTB_OFFSET, UFS_IMG_BOOTB_SECTORS, 0x02, 0x01 },
};

struct ufs_utrl_ring {
	unsigned short		depth;
	unsigned short		head;
	unsigned short		tail;
	unsigned char		next_tag;
	unsigned long		doorbell_writes;
	unsigned long		completed_upius;
	unsigned long		nop_upius;
	unsigned long		cmd_upius;
	unsigned long		query_upius;
	struct ufs_utrd_entry	slots[UFS_UTRL_DEPTH];
};

static int ufs_fd = -1;
static int ufs_online = 0;
static char ufs_img_path[128] = UFSDISKFILE;
static struct ufs_persist_hdr ufs_hdr;
static struct ufs_utrl_ring ufs_utrl;

/* Simulated UFSHCI v4.0 MMIO Registers */
static unsigned int ufs_reg_cap = 0x071f011fU; /* 32 UTRD, 8 UTMRD, 64-bit, UIC */
static unsigned int ufs_reg_ver = UFSHCI_VERSION_4_0;
static unsigned int ufs_reg_hcs = 0x0000000fU; /* DP=1, UTRLRDY=1, UTMRLRDY=1, UCRDY=1 */
static unsigned int ufs_reg_hce = 0x00000001U; /* HCE=1 */

/* Telemetry counters */
static unsigned long ufs_read_cmds = 0;
static unsigned long ufs_write_cmds = 0;
static unsigned long ufs_sectors_read = 0;
static unsigned long ufs_sectors_written = 0;
static unsigned long ufs_flush_cmds = 0;
static unsigned long ufs_unmap_cmds = 0;
static unsigned long ufs_unmap_sectors = 0;
static unsigned long ufs_rpmb_writes = 0;
static unsigned long ufs_rpmb_reads = 0;
static unsigned long ufs_rpmb_auth_fails = 0;

/* UFS Field Firmware Update (FFU via SCSI WRITE_BUFFER 0x3B) state */
static struct fwupd_stream_state ufs_fw_stream;
static char ufs_fw_rev[8] = "4.00";
static unsigned short ufs_device_version = 0x0400;
static unsigned char ufs_fw_sha256[32];
static unsigned int ufs_ffu_count = 0;

static int ufs_sizes[UFS_MAX_MINORS];
static int ufs_blocksizes[UFS_MAX_MINORS];
static struct hd_struct ufs_part[UFS_MAX_MINORS];

static unsigned char ufs_zero_page[4096];
static unsigned char ufs_bounce_buf[4096];

/*
 * FIPS 180-4 SHA-256 implementation for authenticating 512-byte RPMB frames
 */
static const unsigned int ufs_sha256_k[64] = {
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

static inline unsigned int ufs_rotr32(unsigned int x, int n)
{
	return (x >> n) | (x << (32 - n));
}

static void ufs_sha256_transform(unsigned int state[8], const unsigned char block[64])
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
		unsigned int s0 = ufs_rotr32(w[i - 15], 7) ^ ufs_rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
		unsigned int s1 = ufs_rotr32(w[i - 2], 17) ^ ufs_rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}

	a = state[0]; b = state[1]; c = state[2]; d = state[3];
	e = state[4]; f = state[5]; g = state[6]; h = state[7];

	for (i = 0; i < 64; i++) {
		unsigned int S1 = ufs_rotr32(e, 6) ^ ufs_rotr32(e, 11) ^ ufs_rotr32(e, 25);
		unsigned int ch = (e & f) ^ ((~e) & g);
		unsigned int S0 = ufs_rotr32(a, 2) ^ ufs_rotr32(a, 13) ^ ufs_rotr32(a, 22);
		unsigned int maj = (a & b) ^ (a & c) ^ (b & c);
		t1 = h + S1 + ch + ufs_sha256_k[i] + w[i];
		t2 = S0 + maj;
		h = g; g = f; f = e; e = d + t1;
		d = c; c = b; b = a; a = t1 + t2;
	}

	state[0] += a; state[1] += b; state[2] += c; state[3] += d;
	state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

/*
 * Compute 32-byte keyed SHA-256 MAC over the 32-byte RPMB key and the
 * 284-byte authenticated tail of struct ufs_rpmb_frame (starting at frame->data).
 * Total input = 32 + 284 = 316 bytes (5 x 64-byte SHA-256 blocks with padding).
 */
static void ufs_rpmb_compute_mac(const unsigned char key[32],
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
	/* 316 bytes of data -> append 0x80 at byte 316, bit length 316*8 = 2528 (0x09e0) at 318..319 */
	msg[316] = 0x80;
	msg[318] = 0x09;
	msg[319] = 0xe0;

	for (i = 0; i < 5; i++)
		ufs_sha256_transform(state, msg + (i * 64));

	for (i = 0; i < 8; i++) {
		out_mac[i * 4 + 0] = (unsigned char)((state[i] >> 24) & 0xff);
		out_mac[i * 4 + 1] = (unsigned char)((state[i] >> 16) & 0xff);
		out_mac[i * 4 + 2] = (unsigned char)((state[i] >> 8) & 0xff);
		out_mac[i * 4 + 3] = (unsigned char)(state[i] & 0xff);
	}
}

static void ufs_save_persist_hdr(void)
{
	if (ufs_fd < 0)
		return;
	ufs_hdr.magic = UFS_HDR_MAGIC;
	lseek(ufs_fd, (long)UFS_IMG_HDR_OFFSET, 0);
	write(ufs_fd, &ufs_hdr, sizeof(ufs_hdr));
}

static void ufs_load_or_init_persist_hdr(void)
{
	char boot_buf[512];

	if (ufs_fd < 0)
		return;

	memset(&ufs_hdr, 0, sizeof(ufs_hdr));
	lseek(ufs_fd, (long)UFS_IMG_HDR_OFFSET, 0);
	if (read(ufs_fd, &ufs_hdr, sizeof(ufs_hdr)) == (int)sizeof(ufs_hdr) &&
	    ufs_hdr.magic == UFS_HDR_MAGIC) {
		if (ufs_hdr.boot_lun_id != 1 && ufs_hdr.boot_lun_id != 2)
			ufs_hdr.boot_lun_id = 1;
		if (ufs_hdr.fw_rev[0] == '\0') {
			memset(ufs_hdr.fw_rev, 0, sizeof(ufs_hdr.fw_rev));
			strcpy(ufs_hdr.fw_rev, "4.00");
			ufs_hdr.device_version = 0x0400;
			ufs_hdr.ffu_count = 0;
			fwupd_build_bin_image("ufs", FWUPD_DEVID_UFS, FWUPD_GUID_UFS, "4.00",
					      FWUPD_FLAG_SIGNED_PAYLOAD | FWUPD_FLAG_USABLE_DURING_UPDATE,
					      ufs_bounce_buf, ufs_hdr.fw_sha256);
		}
		memset(ufs_fw_rev, 0, sizeof(ufs_fw_rev));
		strncpy(ufs_fw_rev, ufs_hdr.fw_rev, 4);
		ufs_device_version = ufs_hdr.device_version;
		ufs_ffu_count = ufs_hdr.ffu_count;
		memcpy(ufs_fw_sha256, ufs_hdr.fw_sha256, 32);
		ufs_hdr.power_cycles++;
		ufs_save_persist_hdr();
	} else {
		memset(&ufs_hdr, 0, sizeof(ufs_hdr));
		ufs_hdr.magic = UFS_HDR_MAGIC;
		ufs_hdr.boot_lun_id = 1;	/* Boot LUN A (/dev/ufsb) active by default */
		ufs_hdr.write_booster_en = 1;	/* SLC WriteBooster enabled */
		ufs_hdr.boot_lun_en = 1;
		ufs_hdr.active_icc_level = 0x0f;
		ufs_hdr.power_cycles = 1;
		ufs_hdr.rpmb_key_set = 0;
		ufs_hdr.rpmb_write_counter = 0;
		strcpy(ufs_hdr.fw_rev, "4.00");
		ufs_hdr.device_version = 0x0400;
		ufs_hdr.ffu_count = 0;
		memcpy(ufs_hdr.fw_sha256, ufs_fw_sha256, 32);
		ufs_save_persist_hdr();
	}

	/* Ensure Boot LUN A (/dev/ufsb) and Boot LUN B (/dev/ufsc) have boot headers */
	memset(boot_buf, 0, sizeof(boot_buf));
	lseek(ufs_fd, (long)UFS_IMG_BOOTA_OFFSET, 0);
	if (read(ufs_fd, boot_buf, sizeof(boot_buf)) != sizeof(boot_buf) ||
	    strncmp(boot_buf, "ANDROID!", 8) != 0) {
		memset(boot_buf, 0, sizeof(boot_buf));
		strcpy(boot_buf, "ANDROID! slot=a boot_lun=1 (/dev/ufsb) version=UFS4.0-primary-bootloader");
		lseek(ufs_fd, (long)UFS_IMG_BOOTA_OFFSET, 0);
		write(ufs_fd, boot_buf, sizeof(boot_buf));
	}

	memset(boot_buf, 0, sizeof(boot_buf));
	lseek(ufs_fd, (long)UFS_IMG_BOOTB_OFFSET, 0);
	if (read(ufs_fd, boot_buf, sizeof(boot_buf)) != sizeof(boot_buf) ||
	    strncmp(boot_buf, "ANDROID!", 8) != 0) {
		memset(boot_buf, 0, sizeof(boot_buf));
		strcpy(boot_buf, "ANDROID! slot=b boot_lun=2 (/dev/ufsc) version=UFS4.0-secondary-ota-slot");
		lseek(ufs_fd, (long)UFS_IMG_BOOTB_OFFSET, 0);
		write(ufs_fd, boot_buf, sizeof(boot_buf));
	}
}

static void ufs_fill_device_desc(struct ufs_device_desc *d)
{
	memset(d, 0, sizeof(*d));
	d->bLength = (unsigned char)sizeof(*d);
	d->bDescriptorIDN = UFS_DESC_IDN_DEVICE;
	d->bDevice = 0x00;
	d->bDeviceClass = 0x00;
	d->bDeviceSubClass = 0x00;
	d->bProtocol = 0x00;
	d->bNumberLU = UFS_MAX_LUNS;
	d->bNumberWLU = 4;
	d->bBootEnable = ufs_hdr.boot_lun_en;
	d->bDescrAccessEn = 1;
	d->bInitPowerMode = 1;
	d->bHighPriorityLUN = 0x7f;
	d->bSecurityLU = 1;
	d->bInitActiveICCLevel = ufs_hdr.active_icc_level;
	d->wSpecVersion = UFS_SPEC_VERSION_4_0;
	d->wManufactureDate = 0x0926;
	d->wManufacturerID = 0x01ce;
	d->bUD0BaseOffset = 0x1a;
	d->bUDConfigPLength = 0x1a;
	d->bDeviceRTTCap = 2;
	d->bUFSFeaturesSupport = 0x81; /* WriteBooster + HS-G5 */
	d->bQueueDepth = UFS_UTRL_DEPTH;
	d->wDeviceVersion = ufs_device_version;
	d->bNumSecureWPArea = 4;
	d->dExtendedUFSFeaturesSupport = 0x00000100U; /* WriteBooster */
	d->bWriteBoosterBufferPreserveUserSpaceEn = 1;
	d->bWriteBoosterBufferType = 1; /* Shared buffer */
	d->dNumSharedWriteBoosterBufferAllocUnits = 1;
	strcpy(d->manufacturer_name, "SIX-JEDEC");
	strcpy(d->product_name, "SIX-UFS-4.0-5M-CHIP");
	strcpy(d->serial_number, "SIX-UFS4-2026-0001");
	strcpy(d->oem_id, "ANDROID-SIX");
}

static void ufs_fill_geometry_desc(struct ufs_geometry_desc *g)
{
	memset(g, 0, sizeof(*g));
	g->bLength = (unsigned char)sizeof(*g);
	g->bDescriptorIDN = UFS_DESC_IDN_GEOMETRY;
	g->bMediaTechnology = 0x00;
	g->qTotalRawDeviceCapacity = (unsigned int)(UFS_IMG_TOTAL_BYTES / 512UL);
	g->bMaxNumberLU = 8;
	g->dSegmentSize = 8192;
	g->bAllocationUnitSize = 1;
	g->bMinAddrBlockSize = 8;
	g->bOptimalReadBlockSize = 8;
	g->bOptimalWriteBlockSize = 8;
	g->bMaxInBufferSize = 8;
	g->bMaxOutBufferSize = 8;
	g->bRPMB_ReadWriteSize = 64;
	g->wSupportedMemoryTypes = 0x8003;
	g->dWriteBoosterBufferMaxNAllocUnits = 1;
	g->bDeviceMaxWriteBoosterLUs = 1;
	g->bWriteBoosterBufferCapAdjFac = 3;
}

static void ufs_fill_health_desc(struct ufs_health_desc *h)
{
	memset(h, 0, sizeof(*h));
	h->bLength = (unsigned char)sizeof(*h);
	h->bDescriptorIDN = UFS_DESC_IDN_HEALTH;
	h->bPreEOLInfo = 0x01;		/* 0x01 = Normal (<80% reserved blocks) */
	h->bDeviceLifeTimeEstA = 0x01;	/* 0x01 = 0% - 10% SLC wear */
	h->bDeviceLifeTimeEstB = 0x01;	/* 0x01 = 0% - 10% TLC wear */
	h->vendor_read_cmds = (unsigned int)ufs_read_cmds;
	h->vendor_write_cmds = (unsigned int)ufs_write_cmds;
	h->vendor_sectors_read = (unsigned int)ufs_sectors_read;
	h->vendor_sectors_written = (unsigned int)ufs_sectors_written;
	h->vendor_unmap_cmds = (unsigned int)ufs_unmap_cmds;
	h->vendor_unmap_sectors = (unsigned int)ufs_unmap_sectors;
	h->vendor_flush_cmds = (unsigned int)ufs_flush_cmds;
	h->vendor_rpmb_writes = (unsigned int)ufs_rpmb_writes;
	h->vendor_rpmb_reads = (unsigned int)ufs_rpmb_reads;
	h->vendor_rpmb_auth_fails = (unsigned int)ufs_rpmb_auth_fails;
}

static int ufs_fill_unit_desc(unsigned char lun, struct ufs_unit_desc *u)
{
	memset(u, 0, sizeof(*u));
	u->bLength = (unsigned char)sizeof(*u);
	u->bDescriptorIDN = UFS_DESC_IDN_UNIT;
	u->bUnitIndex = lun;
	u->bLUEnable = 1;
	u->bLUQueueDepth = UFS_UTRL_DEPTH;
	u->bDataReliability = 1;
	u->dEraseBlockSize = 4096;
	u->bProvisioningType = 0x02; /* Thin provisioning / UNMAP */

	if (lun < UFS_MAX_LUNS) {
		u->bBootLunID = ufs_luns[lun].boot_lun_id;
		u->bMemoryType = ufs_luns[lun].memory_type;
		u->bLogicalBlockSize = 9; /* 2^9 = 512B */
		u->qLogicalBlockCount = (unsigned int)ufs_luns[lun].nr_sectors;
		u->qPhyMemResourceCount = (unsigned int)ufs_luns[lun].nr_sectors;
		u->dLUNumWriteBoosterBufferAllocUnits = (lun == 0) ? 1 : 0;
		strcpy(u->lun_name, ufs_luns[lun].role);
		return 0;
	} else if (lun == UFS_WLUN_RPMB) {
		u->bBootLunID = 0;
		u->bMemoryType = 0x0f; /* RPMB Secure Memory */
		u->bLogicalBlockSize = 8; /* 2^8 = 256B RPMB blocks */
		u->qLogicalBlockCount = UFS_RPMB_TOTAL_BLOCKS;
		u->qPhyMemResourceCount = UFS_RPMB_TOTAL_BLOCKS;
		strcpy(u->lun_name, "W-LUN RPMB (/dev/ufs-rpmb)");
		return 0;
	}
	return -EINVAL;
}

static void ufs_exec_query_upiu(struct ufs_utrd_entry *e)
{
	unsigned char opcode = e->cdb[0];
	unsigned char idn = e->cdb[1];
	unsigned char index = e->cdb[2];
	unsigned int val = ((unsigned int)e->cdb[8] << 24) |
			   ((unsigned int)e->cdb[9] << 16) |
			   ((unsigned int)e->cdb[10] << 8) |
			   ((unsigned int)e->cdb[11]);
	void *buf = (void *)e->data_addr;

	e->hdr.trans_type = UPIU_TRANSACTION_QUERY_RSP;
	e->hdr.response = 0x00;
	e->hdr.status = 0x00;
	e->ocs = UFS_OCS_SUCCESS;

	switch (opcode) {
	case UPIU_QUERY_OPCODE_NOP:
		return;

	case UPIU_QUERY_OPCODE_READ_DESC:
		if (!buf) {
			e->hdr.response = 0xfe;
			return;
		}
		if (idn == UFS_DESC_IDN_DEVICE) {
			ufs_fill_device_desc((struct ufs_device_desc *)buf);
		} else if (idn == UFS_DESC_IDN_GEOMETRY) {
			ufs_fill_geometry_desc((struct ufs_geometry_desc *)buf);
		} else if (idn == UFS_DESC_IDN_HEALTH) {
			ufs_fill_health_desc((struct ufs_health_desc *)buf);
		} else if (idn == UFS_DESC_IDN_UNIT) {
			if (ufs_fill_unit_desc(index, (struct ufs_unit_desc *)buf) < 0)
				e->hdr.response = 0xf6;
		} else {
			e->hdr.response = 0xf6;
		}
		return;

	case UPIU_QUERY_OPCODE_READ_ATTR:
		if (idn == UFS_ATTR_IDN_BOOT_LUN_EN)
			val = ufs_hdr.boot_lun_en;
		else if (idn == UFS_ATTR_IDN_POWER_MODE)
			val = 0x11; /* FastMode Rx/Tx */
		else if (idn == UFS_ATTR_IDN_ACTIVE_ICC_LVL)
			val = ufs_hdr.active_icc_level;
		else if (idn == UFS_ATTR_IDN_WB_FLUSH_STATUS)
			val = 0x01;
		else if (idn == UFS_ATTR_IDN_AVAIL_WB_BUFF_SIZE)
			val = 0x0a; /* 100% SLC buffer available */
		else if (idn == UFS_ATTR_IDN_CURR_WB_BUFF_SIZE)
			val = ufs_hdr.write_booster_en ? 1 : 0;
		else if (idn == UFS_ATTR_IDN_BOOT_LUN_ID)
			val = ufs_hdr.boot_lun_id;
		else {
			e->hdr.response = 0xf6;
			return;
		}
		e->cdb[8]  = (unsigned char)((val >> 24) & 0xff);
		e->cdb[9]  = (unsigned char)((val >> 16) & 0xff);
		e->cdb[10] = (unsigned char)((val >> 8) & 0xff);
		e->cdb[11] = (unsigned char)(val & 0xff);
		return;

	case UPIU_QUERY_OPCODE_WRITE_ATTR:
		if (idn == UFS_ATTR_IDN_BOOT_LUN_ID) {
			if (val != 1 && val != 2) {
				e->hdr.response = 0xf6;
				return;
			}
			ufs_hdr.boot_lun_id = (unsigned char)val;
			ufs_save_persist_hdr();
		} else if (idn == UFS_ATTR_IDN_BOOT_LUN_EN) {
			ufs_hdr.boot_lun_en = val ? 1 : 0;
			ufs_save_persist_hdr();
		} else if (idn == UFS_ATTR_IDN_ACTIVE_ICC_LVL) {
			ufs_hdr.active_icc_level = (unsigned char)(val & 0x0f);
			ufs_save_persist_hdr();
		} else {
			e->hdr.response = 0xf6;
		}
		return;

	case UPIU_QUERY_OPCODE_READ_FLAG:
		if (idn == UFS_FLAG_IDN_FDEVICEINIT)
			val = 0; /* Device initialization complete */
		else if (idn == UFS_FLAG_IDN_WB_EN)
			val = ufs_hdr.write_booster_en ? 1 : 0;
		else if (idn == UFS_FLAG_IDN_BKOPS_EN)
			val = 1;
		else
			val = 0;
		e->cdb[11] = (unsigned char)(val & 1);
		return;

	case UPIU_QUERY_OPCODE_SET_FLAG:
		if (idn == UFS_FLAG_IDN_WB_EN) {
			ufs_hdr.write_booster_en = 1;
			ufs_save_persist_hdr();
		}
		e->cdb[11] = 1;
		return;

	case UPIU_QUERY_OPCODE_CLEAR_FLAG:
		if (idn == UFS_FLAG_IDN_WB_EN) {
			ufs_hdr.write_booster_en = 0;
			ufs_save_persist_hdr();
		}
		e->cdb[11] = 0;
		return;

	case UPIU_QUERY_OPCODE_TOGGLE_FLAG:
		if (idn == UFS_FLAG_IDN_WB_EN) {
			ufs_hdr.write_booster_en ^= 1;
			ufs_save_persist_hdr();
			e->cdb[11] = ufs_hdr.write_booster_en;
		}
		return;

	default:
		e->hdr.response = 0xfe;
		return;
	}
}

static void ufs_exec_scsi_upiu(struct ufs_utrd_entry *e)
{
	unsigned char lun = e->hdr.lun;
	unsigned char opcode = e->cdb[0];
	unsigned long lba = ((unsigned long)e->cdb[2] << 24) |
			    ((unsigned long)e->cdb[3] << 16) |
			    ((unsigned long)e->cdb[4] << 8) |
			    ((unsigned long)e->cdb[5]);
	unsigned long blocks = ((unsigned long)e->cdb[7] << 8) |
			       ((unsigned long)e->cdb[8]);
	unsigned char *buf = (unsigned char *)e->data_addr;
	unsigned long base_off, max_sectors, byte_len;

	e->hdr.trans_type = UPIU_TRANSACTION_RESPONSE;
	e->hdr.response = 0x00;
	e->hdr.status = 0x00; /* SAM_STAT_GOOD */
	e->ocs = UFS_OCS_SUCCESS;

	/* Map Boot W-LUN (0xB0) to whichever Boot LUN is selected by bBootLunID */
	if (lun == UFS_WLUN_BOOT)
		lun = (ufs_hdr.boot_lun_id == 2) ? UFS_LUN_BOOT_B : UFS_LUN_BOOT_A;

	if (!ufs_online || ufs_fd < 0 || lun >= UFS_MAX_LUNS) {
		e->hdr.response = 0x01;
		e->hdr.status = 0x02; /* CHECK CONDITION */
		return;
	}

	base_off = ufs_luns[lun].byte_offset;
	max_sectors = ufs_luns[lun].nr_sectors;
	byte_len = blocks * 512UL;

	switch (opcode) {
	case UFS_SCSI_TEST_UNIT_READY:
	case UFS_SCSI_SYNC_CACHE_10:
		if (opcode == UFS_SCSI_SYNC_CACHE_10)
			ufs_flush_cmds++;
		return;

	case UFS_SCSI_INQUIRY:
		if (buf && e->exp_data_len >= 36) {
			memset(buf, 0, e->exp_data_len);
			buf[0] = 0x00; /* Direct access block device */
			buf[2] = 0x06; /* SPC-4 */
			buf[3] = 0x02;
			buf[4] = (e->exp_data_len >= 68) ? 63 : 31;
			memcpy(buf + 8,  "SIX-JEDC", 8);
			memcpy(buf + 16, "SIX-UFS-4.0-CHIP", 16);
			memcpy(buf + 32, ufs_fw_rev, 4);
			if (e->exp_data_len >= 68)
				memcpy(buf + 36, ufs_fw_sha256, 32);
		}
		return;

	case UFS_SCSI_WRITE_BUFFER: {
		unsigned char mode = e->cdb[1] & 0x1f;
		unsigned int buf_off = ((unsigned int)e->cdb[3] << 16) |
				       ((unsigned int)e->cdb[4] << 8) |
				       ((unsigned int)e->cdb[5]);
		unsigned int param_len = ((unsigned int)e->cdb[6] << 16) |
					 ((unsigned int)e->cdb[7] << 8) |
					 ((unsigned int)e->cdb[8]);

		if (mode == SCSI_WB_MODE_DOWNLOAD_OFFSET_SAVE ||
		    mode == SCSI_WB_MODE_DOWNLOAD_SAVE) {
			if (fwupd_stream_write_chunk(&ufs_fw_stream, buf_off, buf, param_len) < 0) {
				e->hdr.status = 0x02;
				return;
			}
			if (mode == SCSI_WB_MODE_DOWNLOAD_OFFSET_SAVE)
				return;
		}

		if (mode == SCSI_WB_MODE_ACTIVATE_DEFERRED ||
		    mode == SCSI_WB_MODE_DOWNLOAD_SAVE) {
			unsigned char full_sha256[32];
			char detected_ver[8];

			memset(detected_ver, 0, sizeof(detected_ver));
			if (fwupd_stream_validate_and_finalize(&ufs_fw_stream, FWUPD_DEVID_UFS,
							       4, detected_ver, full_sha256) < 0) {
				printk("ufshcd0: SCSI WRITE_BUFFER FFU REJECTED (.bin integrity / SHA-256 mismatch)\n");
				e->hdr.status = 0x02;
				return;
			}
			if (mode == SCSI_WB_MODE_ACTIVATE_DEFERRED && param_len > 0 && buf &&
			    ((const struct fwupd_bin_hdr *)ufs_fw_stream.hdr_buf)->magic != FWUPD_BIN_MAGIC) {
				const char *vh = (const char *)buf;
				if (vh[0] >= 0x21 && vh[0] <= 0x7e) {
					memset(detected_ver, 0, sizeof(detected_ver));
					strncpy(detected_ver, vh, 4);
					detected_ver[4] = '\0';
				}
			}
			memset(ufs_fw_rev, ' ', 4);
			ufs_fw_rev[4] = '\0';
			strncpy(ufs_fw_rev, detected_ver, 4);
			ufs_fw_rev[4] = '\0';
			memcpy(ufs_fw_sha256, full_sha256, 32);
			if (detected_ver[0] >= '0' && detected_ver[0] <= '9' &&
			    detected_ver[1] >= '0' && detected_ver[1] <= '9' &&
			    detected_ver[2] >= '0' && detected_ver[2] <= '9' &&
			    detected_ver[3] >= '0' && detected_ver[3] <= '9') {
				ufs_device_version = (unsigned short)(
					((unsigned int)(detected_ver[0] - '0') << 12) |
					((unsigned int)(detected_ver[1] - '0') << 8) |
					((unsigned int)(detected_ver[2] - '0') << 4) |
					((unsigned int)(detected_ver[3] - '0')));
			} else if (detected_ver[0] >= '0' && detected_ver[0] <= '9' &&
				   detected_ver[2] >= '0' && detected_ver[2] <= '9') {
				unsigned int maj = (unsigned int)(detected_ver[0] - '0');
				unsigned int min = (unsigned int)(detected_ver[2] - '0');
				unsigned int sub = (detected_ver[3] >= '0' && detected_ver[3] <= '9')
						   ? (unsigned int)(detected_ver[3] - '0') : 0;
				ufs_device_version = (unsigned short)((maj << 8) | (min << 4) | sub);
			}
			ufs_ffu_count++;
			memset(ufs_hdr.fw_rev, 0, sizeof(ufs_hdr.fw_rev));
			strncpy(ufs_hdr.fw_rev, ufs_fw_rev, 4);
			ufs_hdr.device_version = ufs_device_version;
			ufs_hdr.ffu_count = (unsigned short)ufs_ffu_count;
			memcpy(ufs_hdr.fw_sha256, ufs_fw_sha256, 32);
			ufs_save_persist_hdr();
			printk("ufshcd0: SCSI WRITE_BUFFER FFU activated .bin rev=%s (%u bytes, wDeviceVersion=0x%04x)\n",
			       ufs_fw_rev, ufs_fw_stream.staged_len, ufs_device_version);
			return;
		}

		e->hdr.status = 0x02;
		return;
	}

	case UFS_SCSI_READ_BUFFER:
		if (buf && e->exp_data_len >= 32)
			memcpy(buf, ufs_fw_sha256, 32);
		return;

	case UFS_SCSI_READ_CAPACITY_10:
		if (buf && e->exp_data_len >= 8) {
			unsigned int last_lba = (unsigned int)(max_sectors - 1);
			buf[0] = (unsigned char)((last_lba >> 24) & 0xff);
			buf[1] = (unsigned char)((last_lba >> 16) & 0xff);
			buf[2] = (unsigned char)((last_lba >> 8) & 0xff);
			buf[3] = (unsigned char)(last_lba & 0xff);
			buf[4] = 0; buf[5] = 0; buf[6] = 0x02; buf[7] = 0x00; /* 512B */
		}
		return;

	case UFS_SCSI_READ_10:
		if (!buf || lba + blocks > max_sectors) {
			e->hdr.status = 0x02;
			return;
		}
		lseek(ufs_fd, (long)(base_off + lba * 512UL), 0);
		if (read(ufs_fd, buf, (int)byte_len) != (int)byte_len) {
			e->hdr.status = 0x02;
			return;
		}
		ufs_read_cmds++;
		ufs_sectors_read += blocks;
		return;

	case UFS_SCSI_WRITE_10:
		if (!buf || lba + blocks > max_sectors) {
			e->hdr.status = 0x02;
			return;
		}
		lseek(ufs_fd, (long)(base_off + lba * 512UL), 0);
		if (write(ufs_fd, buf, (int)byte_len) != (int)byte_len) {
			e->hdr.status = 0x02;
			return;
		}
		ufs_write_cmds++;
		ufs_sectors_written += blocks;
		return;

	case UFS_SCSI_UNMAP: {
		unsigned long rem = blocks;
		unsigned long cur = lba;
		if (lba + blocks > max_sectors) {
			e->hdr.status = 0x02;
			return;
		}
		lseek(ufs_fd, (long)(base_off + cur * 512UL), 0);
		while (rem > 0) {
			unsigned long chunk = (rem > 8) ? 8 : rem;
			write(ufs_fd, ufs_zero_page, (int)(chunk * 512UL));
			rem -= chunk;
		}
		ufs_unmap_cmds++;
		ufs_unmap_sectors += blocks;
		return;
	}

	default:
		e->hdr.status = 0x02;
		return;
	}
}

/*
 * Emulate ringing the UFSHCI UTP Transfer Request List Doorbell Register
 * (REG_UTRLDBR = 0x58).
 */
static void ufs_ring_utrl_doorbell(void)
{
	ufs_utrl.doorbell_writes++;

	while (ufs_utrl.head != ufs_utrl.tail) {
		struct ufs_utrd_entry *e = &ufs_utrl.slots[ufs_utrl.head];
		unsigned char ttype = e->hdr.trans_type;

		ufs_utrl.head = (unsigned short)((ufs_utrl.head + 1) % ufs_utrl.depth);

		if (ttype == UPIU_TRANSACTION_NOP_OUT) {
			e->hdr.trans_type = UPIU_TRANSACTION_NOP_IN;
			e->hdr.response = 0x00;
			e->ocs = UFS_OCS_SUCCESS;
			ufs_utrl.nop_upius++;
		} else if (ttype == UPIU_TRANSACTION_QUERY_REQ) {
			ufs_exec_query_upiu(e);
			ufs_utrl.query_upius++;
		} else if (ttype == UPIU_TRANSACTION_COMMAND) {
			ufs_exec_scsi_upiu(e);
			ufs_utrl.cmd_upius++;
		} else {
			e->hdr.response = 0xfe;
			e->ocs = UFS_OCS_INVALID_CMD_TABLE_ATTR;
		}
		ufs_utrl.completed_upius++;
	}
}

static int ufs_submit_upiu(struct ufs_utrd_entry *req)
{
	unsigned short slot = ufs_utrl.tail;

	req->hdr.task_tag = ++ufs_utrl.next_tag;
	ufs_utrl.slots[slot] = *req;
	ufs_utrl.tail = (unsigned short)((ufs_utrl.tail + 1) % ufs_utrl.depth);

	ufs_ring_utrl_doorbell();

	*req = ufs_utrl.slots[slot];
	if (req->ocs != UFS_OCS_SUCCESS || req->hdr.response != 0x00 || req->hdr.status != 0x00)
		return -EIO;
	return 0;
}

static int ufs_submit_scsi_rw(int lun, int cmd_dir, unsigned long lba,
			      unsigned long nsect, unsigned char *buf)
{
	struct ufs_utrd_entry utrd;

	memset(&utrd, 0, sizeof(utrd));
	utrd.hdr.trans_type = UPIU_TRANSACTION_COMMAND;
	utrd.hdr.flags = (cmd_dir == WRITE) ? 0x20 : 0x40;
	utrd.hdr.lun = (unsigned char)lun;
	utrd.exp_data_len = (unsigned int)(nsect * 512UL);
	utrd.data_addr = (unsigned long)buf;

	utrd.cdb[0] = (cmd_dir == WRITE) ? UFS_SCSI_WRITE_10 : UFS_SCSI_READ_10;
	utrd.cdb[2] = (unsigned char)((lba >> 24) & 0xff);
	utrd.cdb[3] = (unsigned char)((lba >> 16) & 0xff);
	utrd.cdb[4] = (unsigned char)((lba >> 8) & 0xff);
	utrd.cdb[5] = (unsigned char)(lba & 0xff);
	utrd.cdb[7] = (unsigned char)((nsect >> 8) & 0xff);
	utrd.cdb[8] = (unsigned char)(nsect & 0xff);

	return ufs_submit_upiu(&utrd);
}

/*
 * Direct sector read/write helper for Device Mapper (dm) stacked on /dev/ufs[a-c].
 */
int ufs_rw_sector(int minor, unsigned long phys_sec,
		  unsigned char *buf, int cmd_dir)
{
	struct buffer_head *bh;
	unsigned long block_nr = phys_sec >> 1;
	int sub_off = (phys_sec & 1) << 9;
	kdev_t kdev = MKDEV(UFS_MAJOR, minor);

	if (!ufs_online || ufs_fd < 0 || minor < 0 || minor >= UFS_MAX_LUNS)
		return -ENODEV;
	if (phys_sec >= ufs_luns[minor].nr_sectors)
		return -EIO;

	bh = get_hash_table(kdev, block_nr, 1024);
	if (cmd_dir == READ) {
		if (bh && buffer_uptodate(bh) && buffer_dirty(bh)) {
			memcpy(buf, bh->b_data + sub_off, 512);
			brelse(bh);
			return 0;
		}
		if (bh)
			brelse(bh);
		return ufs_submit_scsi_rw(minor, READ, phys_sec, 1, buf);
	} else {
		if (is_read_only(kdev)) {
			if (bh)
				brelse(bh);
			return -EROFS;
		}
		if (ufs_submit_scsi_rw(minor, WRITE, phys_sec, 1, buf) < 0) {
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

void do_ufs_request(void)
{
	while (1) {
		int minor;
		unsigned long nsect;

		INIT_REQUEST;

		minor = MINOR(CURRENT->rq_dev);
		if (!ufs_online || ufs_fd < 0 || minor < 0 || minor >= UFS_MAX_LUNS) {
			end_request(0);
			continue;
		}

		nsect = CURRENT->current_nr_sectors;
		if (CURRENT->sector + nsect > ufs_luns[minor].nr_sectors) {
			end_request(0);
			continue;
		}

		if (CURRENT->cmd != READ && CURRENT->cmd != WRITE) {
			end_request(0);
			continue;
		}

		if (CURRENT->cmd == WRITE && is_read_only(CURRENT->rq_dev)) {
			end_request(0);
			continue;
		}

		if (ufs_submit_scsi_rw(minor, CURRENT->cmd, CURRENT->sector,
				       nsect, (unsigned char *)CURRENT->buffer) < 0) {
			end_request(0);
			continue;
		}

		if (CURRENT->cmd == WRITE) {
			extern void dm_notify_bdev_write(kdev_t bdev);
			dm_notify_bdev_write(CURRENT->rq_dev);
		}

		CURRENT->sector += nsect;
		CURRENT->buffer += (nsect << 9);
		CURRENT->nr_sectors -= nsect;
		CURRENT->current_nr_sectors = 0;
		end_request(1);
	}
}

static int ufs_handle_query_ioctl(unsigned long arg)
{
	struct ufs_bsg_query_ioctl q;
	struct ufs_utrd_entry utrd;
	unsigned int xfer_len;
	int err, rc;

	if (!arg)
		return -EINVAL;
	err = verify_area(VERIFY_WRITE, (void *)arg, sizeof(q));
	if (err)
		return err;
	memcpy_fromfs(&q, (void *)arg, sizeof(q));

	xfer_len = q.buf_len;
	if (xfer_len > sizeof(ufs_bounce_buf))
		xfer_len = sizeof(ufs_bounce_buf);
	memset(ufs_bounce_buf, 0, sizeof(ufs_bounce_buf));

	memset(&utrd, 0, sizeof(utrd));
	utrd.hdr.trans_type = UPIU_TRANSACTION_QUERY_REQ;
	utrd.hdr.query_func = (q.opcode == UPIU_QUERY_OPCODE_WRITE_DESC ||
			       q.opcode == UPIU_QUERY_OPCODE_WRITE_ATTR ||
			       q.opcode == UPIU_QUERY_OPCODE_SET_FLAG ||
			       q.opcode == UPIU_QUERY_OPCODE_CLEAR_FLAG ||
			       q.opcode == UPIU_QUERY_OPCODE_TOGGLE_FLAG)
			      ? UPIU_QUERY_FUNC_STANDARD_WRITE_REQ
			      : UPIU_QUERY_FUNC_STANDARD_READ_REQ;
	utrd.cdb[0] = q.opcode;
	utrd.cdb[1] = q.idn;
	utrd.cdb[2] = q.index;
	utrd.cdb[3] = q.selector;
	utrd.cdb[8]  = (unsigned char)((q.value >> 24) & 0xff);
	utrd.cdb[9]  = (unsigned char)((q.value >> 16) & 0xff);
	utrd.cdb[10] = (unsigned char)((q.value >> 8) & 0xff);
	utrd.cdb[11] = (unsigned char)(q.value & 0xff);
	utrd.data_addr = (unsigned long)ufs_bounce_buf;
	utrd.exp_data_len = xfer_len;

	rc = ufs_submit_upiu(&utrd);
	q.response = utrd.hdr.response;
	q.value = ((unsigned int)utrd.cdb[8] << 24) |
		  ((unsigned int)utrd.cdb[9] << 16) |
		  ((unsigned int)utrd.cdb[10] << 8) |
		  ((unsigned int)utrd.cdb[11]);

	if (rc == 0 && xfer_len > 0 && q.buf_addr) {
		err = verify_area(VERIFY_WRITE, (void *)q.buf_addr, xfer_len);
		if (err)
			return err;
		memcpy_tofs((void *)q.buf_addr, ufs_bounce_buf, xfer_len);
	}

	memcpy_tofs((void *)arg, &q, sizeof(q));
	return rc;
}

static int ufs_handle_scsi_ioctl(unsigned long arg)
{
	struct ufs_bsg_scsi_ioctl sc;
	struct ufs_utrd_entry utrd;
	unsigned int xfer_len;
	int err, rc, is_write;

	if (!arg)
		return -EINVAL;
	err = verify_area(VERIFY_WRITE, (void *)arg, sizeof(sc));
	if (err)
		return err;
	memcpy_fromfs(&sc, (void *)arg, sizeof(sc));

	xfer_len = sc.data_len;
	if (xfer_len > sizeof(ufs_bounce_buf))
		xfer_len = sizeof(ufs_bounce_buf);
	memset(ufs_bounce_buf, 0, sizeof(ufs_bounce_buf));

	is_write = (sc.opcode == UFS_SCSI_WRITE_10 ||
		    sc.opcode == UFS_SCSI_WRITE_BUFFER ||
		    sc.opcode == UFS_SCSI_SECURITY_PROT_OUT);

	if (xfer_len > 0 && sc.data_addr && is_write) {
		err = verify_area(VERIFY_READ, (void *)sc.data_addr, xfer_len);
		if (err)
			return err;
		memcpy_fromfs(ufs_bounce_buf, (void *)sc.data_addr, xfer_len);
	}

	memset(&utrd, 0, sizeof(utrd));
	utrd.hdr.trans_type = UPIU_TRANSACTION_COMMAND;
	utrd.hdr.lun = sc.lun;
	utrd.exp_data_len = xfer_len;
	utrd.data_addr = (unsigned long)ufs_bounce_buf;
	utrd.cdb[0] = sc.opcode;
	if (sc.opcode == UFS_SCSI_WRITE_BUFFER || sc.opcode == UFS_SCSI_READ_BUFFER) {
		utrd.cdb[1] = (unsigned char)(sc.rsvd & 0x1f);
		utrd.cdb[2] = (unsigned char)((sc.rsvd >> 8) & 0xff);
		utrd.cdb[3] = (unsigned char)((sc.lba >> 16) & 0xff);
		utrd.cdb[4] = (unsigned char)((sc.lba >> 8) & 0xff);
		utrd.cdb[5] = (unsigned char)(sc.lba & 0xff);
		utrd.cdb[6] = (unsigned char)((xfer_len >> 16) & 0xff);
		utrd.cdb[7] = (unsigned char)((xfer_len >> 8) & 0xff);
		utrd.cdb[8] = (unsigned char)(xfer_len & 0xff);
	} else {
		utrd.cdb[2] = (unsigned char)((sc.lba >> 24) & 0xff);
		utrd.cdb[3] = (unsigned char)((sc.lba >> 16) & 0xff);
		utrd.cdb[4] = (unsigned char)((sc.lba >> 8) & 0xff);
		utrd.cdb[5] = (unsigned char)(sc.lba & 0xff);
		utrd.cdb[7] = (unsigned char)((sc.blocks >> 8) & 0xff);
		utrd.cdb[8] = (unsigned char)(sc.blocks & 0xff);
	}

	rc = ufs_submit_upiu(&utrd);
	sc.status = utrd.hdr.status;

	if (rc == 0 && xfer_len > 0 && sc.data_addr && !is_write) {
		err = verify_area(VERIFY_WRITE, (void *)sc.data_addr, xfer_len);
		if (err)
			return err;
		memcpy_tofs((void *)sc.data_addr, ufs_bounce_buf, xfer_len);
	}

	memcpy_tofs((void *)arg, &sc, sizeof(sc));
	return rc;
}

static int ufs_handle_rpmb_ioctl(unsigned long arg)
{
	struct ufs_rpmb_frame frame;
	unsigned char expected_mac[32];
	int err;

	if (!arg)
		return -EINVAL;
	if (!ufs_online || ufs_fd < 0)
		return -ENODEV;

	err = verify_area(VERIFY_WRITE, (void *)arg, sizeof(frame));
	if (err)
		return err;
	memcpy_fromfs(&frame, (void *)arg, sizeof(frame));

	switch (frame.req_resp) {
	case RPMB_REQ_PROGRAM_KEY:
		memcpy(ufs_hdr.rpmb_key, frame.key_mac, 32);
		ufs_hdr.rpmb_key_set = 1;
		ufs_save_persist_hdr();
		memset(frame.key_mac, 0, 32);
		frame.result = RPMB_RES_OK;
		frame.req_resp = RPMB_RESP_PROGRAM_KEY;
		break;

	case RPMB_REQ_GET_COUNTER:
		frame.req_resp = RPMB_RESP_GET_COUNTER;
		if (!ufs_hdr.rpmb_key_set) {
			frame.write_counter = 0;
			frame.result = RPMB_RES_NO_AUTH_KEY;
		} else {
			frame.write_counter = ufs_hdr.rpmb_write_counter;
			frame.result = RPMB_RES_OK;
			ufs_rpmb_compute_mac(ufs_hdr.rpmb_key, &frame, frame.key_mac);
		}
		break;

	case RPMB_REQ_WRITE_DATA:
		if (!ufs_hdr.rpmb_key_set) {
			frame.req_resp = RPMB_RESP_WRITE_DATA;
			frame.result = RPMB_RES_NO_AUTH_KEY;
			break;
		}
		if (frame.addr >= UFS_RPMB_TOTAL_BLOCKS) {
			frame.req_resp = RPMB_RESP_WRITE_DATA;
			frame.result = RPMB_RES_ADDR_FAILURE;
			break;
		}
		/* Verify keyed SHA-256 MAC over request frame */
		ufs_rpmb_compute_mac(ufs_hdr.rpmb_key, &frame, expected_mac);
		if (memcmp(expected_mac, frame.key_mac, 32) != 0) {
			ufs_rpmb_auth_fails++;
			frame.req_resp = RPMB_RESP_WRITE_DATA;
			frame.result = RPMB_RES_AUTH_FAILURE;
			break;
		}
		if (frame.write_counter != ufs_hdr.rpmb_write_counter) {
			ufs_rpmb_auth_fails++;
			frame.req_resp = RPMB_RESP_WRITE_DATA;
			frame.result = RPMB_RES_COUNTER_FAILURE;
			break;
		}
		lseek(ufs_fd, (long)(UFS_IMG_RPMB_OFFSET + (unsigned long)frame.addr * UFS_RPMB_BLOCK_SIZE), 0);
		if (write(ufs_fd, frame.data, UFS_RPMB_BLOCK_SIZE) != UFS_RPMB_BLOCK_SIZE) {
			frame.req_resp = RPMB_RESP_WRITE_DATA;
			frame.result = RPMB_RES_WRITE_FAILURE;
			break;
		}
		ufs_hdr.rpmb_write_counter++;
		ufs_rpmb_writes++;
		ufs_save_persist_hdr();

		frame.write_counter = ufs_hdr.rpmb_write_counter;
		frame.result = RPMB_RES_OK;
		frame.req_resp = RPMB_RESP_WRITE_DATA;
		ufs_rpmb_compute_mac(ufs_hdr.rpmb_key, &frame, frame.key_mac);
		break;

	case RPMB_REQ_READ_DATA:
		if (frame.addr >= UFS_RPMB_TOTAL_BLOCKS) {
			frame.req_resp = RPMB_RESP_READ_DATA;
			frame.result = RPMB_RES_ADDR_FAILURE;
			break;
		}
		lseek(ufs_fd, (long)(UFS_IMG_RPMB_OFFSET + (unsigned long)frame.addr * UFS_RPMB_BLOCK_SIZE), 0);
		if (read(ufs_fd, frame.data, UFS_RPMB_BLOCK_SIZE) != UFS_RPMB_BLOCK_SIZE) {
			frame.req_resp = RPMB_RESP_READ_DATA;
			frame.result = RPMB_RES_READ_FAILURE;
			break;
		}
		ufs_rpmb_reads++;
		frame.write_counter = ufs_hdr.rpmb_write_counter;
		frame.result = RPMB_RES_OK;
		frame.req_resp = RPMB_RESP_READ_DATA;
		if (ufs_hdr.rpmb_key_set)
			ufs_rpmb_compute_mac(ufs_hdr.rpmb_key, &frame, frame.key_mac);
		break;

	default:
		frame.result = RPMB_RES_GENERAL_FAILURE;
		break;
	}

	memcpy_tofs((void *)arg, &frame, sizeof(frame));
	return (frame.result == RPMB_RES_OK) ? 0 : -EIO;
}

static int ufs_common_ioctl(struct inode *inode, struct file *file,
			    unsigned int cmd, unsigned long arg, int is_chr)
{
	int minor = inode ? MINOR(inode->i_rdev) : 0;
	int err;

	switch (cmd) {
	case UFS_IOCTL_QUERY:
		return ufs_handle_query_ioctl(arg);

	case UFS_IOCTL_SCSI_CMD:
		return ufs_handle_scsi_ioctl(arg);

	case UFS_IOCTL_RPMB:
		return ufs_handle_rpmb_ioctl(arg);

	case UFS_IOCTL_RESET:
		if (!suser())
			return -EACCES;
		ufs_utrl.head = 0;
		ufs_utrl.tail = 0;
		ufs_hdr.power_cycles++;
		ufs_save_persist_hdr();
		return 0;

	case BLKGETSIZE:
		if (is_chr || minor < 0 || minor >= UFS_MAX_LUNS || !arg)
			return -EINVAL;
		err = verify_area(VERIFY_WRITE, (long *)arg, sizeof(long));
		if (err)
			return err;
		put_fs_long(ufs_luns[minor].nr_sectors, (long *)arg);
		return 0;

	case BLKFLSBUF:
		if (!suser())
			return -EACCES;
		if (!is_chr && inode) {
			fsync_dev(inode->i_rdev);
			invalidate_buffers(inode->i_rdev);
		}
		ufs_flush_cmds++;
		return 0;

	RO_IOCTLS(inode->i_rdev, arg);

	default:
		return -EINVAL;
	}
}

static int ufs_blk_ioctl(struct inode *inode, struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	if (!inode)
		return -EINVAL;
	return ufs_common_ioctl(inode, file, cmd, arg, 0);
}

static int ufs_chr_ioctl(struct inode *inode, struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	if (!inode)
		return -EINVAL;
	return ufs_common_ioctl(inode, file, cmd, arg, 1);
}

static int ufs_blk_open(struct inode *inode, struct file *filp)
{
	int minor;

	if (!inode)
		return -EINVAL;
	minor = MINOR(inode->i_rdev);
	if (minor < 0 || minor >= UFS_MAX_LUNS)
		return -ENODEV;
	if (!ufs_online || ufs_fd < 0)
		return -ENODEV;
	return 0;
}

static void ufs_blk_release(struct inode *inode, struct file *filp)
{
	if (!inode)
		return;
	sync_dev(inode->i_rdev);
}

static int ufs_chr_open(struct inode *inode, struct file *filp)
{
	int minor;

	if (!inode)
		return -ENODEV;
	minor = MINOR(inode->i_rdev);
	if (minor != UFS_CHR_MINOR_BSG && minor != UFS_CHR_MINOR_RPMB)
		return -ENODEV;
	if (!ufs_online || ufs_fd < 0)
		return -ENODEV;
	return 0;
}

static void ufs_chr_release(struct inode *inode, struct file *filp)
{
}

static struct file_operations ufs_blk_fops = {
	NULL,			/* lseek */
	block_read,		/* read */
	block_write,		/* write */
	NULL,			/* readdir */
	NULL,			/* select */
	ufs_blk_ioctl,		/* ioctl */
	NULL,			/* mmap */
	ufs_blk_open,		/* open */
	ufs_blk_release,	/* release */
	block_fsync		/* fsync */
};

static struct file_operations ufs_chr_fops = {
	NULL,			/* lseek */
	NULL,			/* read */
	NULL,			/* write */
	NULL,			/* readdir */
	NULL,			/* select */
	ufs_chr_ioctl,		/* ioctl */
	NULL,			/* mmap */
	ufs_chr_open,		/* open */
	ufs_chr_release,	/* release */
	NULL			/* fsync */
};

static void ufs_geninit(struct gendisk *gd)
{
	int i;

	for (i = 0; i < UFS_MAX_MINORS; i++) {
		if (i < UFS_MAX_LUNS && ufs_online) {
			ufs_part[i].start_sect = 0;
			ufs_part[i].nr_sects = ufs_luns[i].nr_sectors;
			ufs_sizes[i] = (int)(ufs_luns[i].nr_sectors >> (BLOCK_SIZE_BITS - 9));
		} else {
			ufs_part[i].start_sect = 0;
			ufs_part[i].nr_sects = 0;
			ufs_sizes[i] = 0;
		}
		ufs_blocksizes[i] = 1024;
	}
	blk_size[UFS_MAJOR] = ufs_sizes;
	blksize_size[UFS_MAJOR] = ufs_blocksizes;
}

static struct gendisk ufs_gendisk = {
	UFS_MAJOR,		/* major */
	"ufs",			/* major_name */
	0,			/* minor_shift */
	1,			/* max_p */
	UFS_MAX_LUNS,		/* max_nr */
	ufs_geninit,		/* init */
	ufs_part,		/* part */
	ufs_sizes,		/* sizes */
	UFS_MAX_LUNS,		/* nr_real */
	NULL,			/* real_devices */
	NULL			/* next */
};

int get_ufs_proc_info(char *buf)
{
	int len = 0;
	const char *active_slot = (ufs_hdr.boot_lun_id == 2)
				  ? "Slot B (/dev/ufsc, LUN 2)"
				  : "Slot A (/dev/ufsb, LUN 1)";

	len += sprintf(buf + len,
		"UFS Host Controller: /dev/ufs-bsg0 (char %d:0, UFSHCI v4.0, MIPI UniPro/M-PHY)\n"
		"Model / Serial:      SIX-UFS-4.0-5M-CHIP / SIX-UFS4-2026-0001 (JEDEC UFS 4.0, FW %s)\n"
		"UniPro Link State:   HS-Gear5 Rate-B (2 Lanes TX/RX, FastMode 0x11)\n"
		"Controller Regs:     CAP=0x%08x VER=0x%08x HCS=0x%08x HCE=0x%08x\n"
		"Host Flash Image:    %s (%lu KB / %lu MB unified chip image)\n"
		"Active Boot LUN:     bBootLunID=0x%02x -> %s\n"
		"WriteBooster SLC:    fWriteBoosterEn=%u (%s, 100%% buffer available)\n"
		"\n"
		"UTRL Ring   Depth  Head  Tail  DB_Writes  Completed  NOP_UPIU  CMD_UPIU  QUERY_UPIU\n"
		"UTRD (Q0)   %-5u  %-4u  %-4u  %-9lu  %-9lu  %-8lu  %-8lu  %lu\n"
		"\n"
		"Logical Units (Multi-LUN Architecture):\n"
		"  LUN     Node           Maj:Min  Offset      Capacity             Role\n"
		"  LUN 0   /dev/ufsa      %d:0     0x%06lx    %4lu sectors (4096K) %s\n"
		"  LUN 1   /dev/ufsb      %d:1     0x%06lx    %4lu sectors ( 384K) %s%s\n"
		"  LUN 2   /dev/ufsc      %d:2     0x%06lx    %4lu sectors ( 384K) %s%s\n"
		"  W-LUN   /dev/ufs-rpmb  %d:1     0x%06lx     512 blocks  ( 128K) Replay Protected Memory Block\n"
		"\n"
		"Health Descriptor (IDN 0x09) & RPMB Security Status:\n"
		"  Pre-EOL Info:         0x01 (Normal)\n"
		"  LifeTime Est A / B:   0x01 / 0x01 (0%% - 10%% wear)\n"
		"  SCSI Reads / Writes:  %lu (%lu sectors) / %lu (%lu sectors)\n"
		"  Flush / UNMAP Cmds:   %lu / %lu (%lu sectors unmapped)\n"
		"  RPMB Auth Key:        %s (Write Counter: %u)\n"
		"  RPMB Operations:      %lu writes, %lu reads, %lu auth failures\n",
		UFS_CHR_MAJOR,
		ufs_fw_rev,
		ufs_reg_cap, ufs_reg_ver, ufs_reg_hcs, ufs_reg_hce,
		ufs_img_path, UFS_IMG_TOTAL_BYTES >> 10, UFS_IMG_TOTAL_BYTES >> 20,
		ufs_hdr.boot_lun_id, active_slot,
		ufs_hdr.write_booster_en, ufs_hdr.write_booster_en ? "ENABLED" : "DISABLED",
		ufs_utrl.depth, ufs_utrl.head, ufs_utrl.tail,
		ufs_utrl.doorbell_writes, ufs_utrl.completed_upius,
		ufs_utrl.nop_upius, ufs_utrl.cmd_upius, ufs_utrl.query_upius,
		UFS_MAJOR, UFS_IMG_LUN0_OFFSET, UFS_IMG_LUN0_SECTORS, ufs_luns[0].role,
		UFS_MAJOR, UFS_IMG_BOOTA_OFFSET, UFS_IMG_BOOTA_SECTORS, ufs_luns[1].role,
		(ufs_hdr.boot_lun_id == 1) ? " [ACTIVE]" : "",
		UFS_MAJOR, UFS_IMG_BOOTB_OFFSET, UFS_IMG_BOOTB_SECTORS, ufs_luns[2].role,
		(ufs_hdr.boot_lun_id == 2) ? " [ACTIVE]" : "",
		UFS_CHR_MAJOR, UFS_IMG_RPMB_OFFSET,
		ufs_read_cmds, ufs_sectors_read, ufs_write_cmds, ufs_sectors_written,
		ufs_flush_cmds, ufs_unmap_cmds, ufs_unmap_sectors,
		ufs_hdr.rpmb_key_set ? "PROGRAMMED" : "NOT PROGRAMMED",
		ufs_hdr.rpmb_write_counter,
		ufs_rpmb_writes, ufs_rpmb_reads, ufs_rpmb_auth_fails);

	return len;
}

int ufs_init(void)
{
	extern char *getenv(const char *name);
	extern int ftruncate(int fd, unsigned long length);
	const char *env_path;
	long sz;

	memset(&ufs_utrl, 0, sizeof(ufs_utrl));
	ufs_utrl.depth = UFS_UTRL_DEPTH;

	fwupd_build_bin_image("ufs", FWUPD_DEVID_UFS, FWUPD_GUID_UFS, "4.00",
			      FWUPD_FLAG_SIGNED_PAYLOAD | FWUPD_FLAG_USABLE_DURING_UPDATE,
			      ufs_bounce_buf, ufs_fw_sha256);

	env_path = getenv("UFSDISKFILE");
	if (env_path && env_path[0]) {
		strncpy(ufs_img_path, env_path, sizeof(ufs_img_path) - 1);
		ufs_img_path[sizeof(ufs_img_path) - 1] = '\0';
	}

	ufs_fd = open(ufs_img_path, 2 | 0100, 0644); /* O_RDWR | O_CREAT */
	if (ufs_fd >= 0) {
		sz = lseek(ufs_fd, 0L, 2);
		if (sz < (long)UFS_IMG_TOTAL_BYTES) {
			ftruncate(ufs_fd, UFS_IMG_TOTAL_BYTES);
		}
		ufs_online = 1;
		ufs_load_or_init_persist_hdr();
	} else {
		printk("ufshcd0: warning: could not open backing file %s\n", ufs_img_path);
		ufs_online = 0;
	}

	if (register_chrdev(UFS_CHR_MAJOR, "ufs", &ufs_chr_fops)) {
		printk("ufshcd0: unable to register char major %d\n", UFS_CHR_MAJOR);
	}
	if (register_blkdev(UFS_MAJOR, "ufs", &ufs_blk_fops)) {
		printk("ufshcd0: unable to register block major %d\n", UFS_MAJOR);
		return -1;
	}

	blk_dev[UFS_MAJOR].request_fn = DEVICE_REQUEST;
	read_ahead[UFS_MAJOR] = 8;
	ufs_geninit(&ufs_gendisk);
	ufs_gendisk.next = gendisk_head;
	gendisk_head = &ufs_gendisk;

	/* Execute UFS 4.0 link startup handshake: NOP OUT/IN + fDeviceInit + READ_DESC */
	if (ufs_online) {
		struct ufs_utrd_entry utrd;

		memset(&utrd, 0, sizeof(utrd));
		utrd.hdr.trans_type = UPIU_TRANSACTION_NOP_OUT;
		ufs_submit_upiu(&utrd);

		memset(&utrd, 0, sizeof(utrd));
		utrd.hdr.trans_type = UPIU_TRANSACTION_QUERY_REQ;
		utrd.hdr.query_func = UPIU_QUERY_FUNC_STANDARD_WRITE_REQ;
		utrd.cdb[0] = UPIU_QUERY_OPCODE_SET_FLAG;
		utrd.cdb[1] = UFS_FLAG_IDN_FDEVICEINIT;
		ufs_submit_upiu(&utrd);

		memset(&utrd, 0, sizeof(utrd));
		utrd.hdr.trans_type = UPIU_TRANSACTION_QUERY_REQ;
		utrd.hdr.query_func = UPIU_QUERY_FUNC_STANDARD_READ_REQ;
		utrd.cdb[0] = UPIU_QUERY_OPCODE_READ_DESC;
		utrd.cdb[1] = UFS_DESC_IDN_DEVICE;
		utrd.data_addr = (unsigned long)ufs_bounce_buf;
		utrd.exp_data_len = sizeof(struct ufs_device_desc);
		ufs_submit_upiu(&utrd);

		printk("ufshcd ufshcd0: JEDEC UFS 4.0 controller initialized (HS-G5 2-Lane, UTRL=%u, host=%s)\n",
		       UFS_UTRL_DEPTH, ufs_img_path);
		printk("ufshcd0: ufsa (4096 KB Data), ufsb (384 KB Boot A), ufsc (384 KB Boot B), ufs-rpmb (128 KB RPMB)\n");
	}

	return 0;
}
