/*
 * include/linux/fwupd.h
 *
 * Firmware Update (fwupd / LVFS) Binary Image (.bin) Format, Streaming
 * FIPS 180-4 SHA-256 Engine, and Plugin Protocol Definitions for SIX.
 *
 * Supports both:
 *   1. Standard controller firmware .bin images (with ARM Cortex-R5 reset
 *      vector + embedded controller microcode descriptor `struct fwupd_bin_hdr`)
 *   2. Arbitrary real-world vendor firmware .bin files of any size streamed
 *      in 512-byte chunks via NVMe Admin Download (0x11) or SCSI WRITE_BUFFER (0x3B)
 */

#ifndef _LINUX_FWUPD_H
#define _LINUX_FWUPD_H

#define FWUPD_ARM_RESET_VECTOR		0xea000026U	/* ARM32: b 0xa0 (branch past 160B hdr) */
#define FWUPD_BIN_MAGIC			0x4d574653U	/* "SFWM" (Storage Firmware Microcode) */
#define FWUPD_CAB_MAGIC			0x4643534dU	/* "MSCF" (Microsoft Cabinet Archive) */
#define FWUPD_UFSH_MAGIC		0x48534655U	/* "UFSH" (Samsung JEDEC UFS Firmware) */
#define FWUPD_BIN_VERSION		1
#define FWUPD_DEFAULT_PAYLOAD_SIZE	1024
#define FWUPD_CHUNK_SIZE		512
#define FWUPD_HDR_INSPECT_LEN		256

/* Backward-compatible aliases */
#define FWUPD_CAPSULE_MAGIC		FWUPD_BIN_MAGIC
#define FWUPD_CAPSULE_VERSION		FWUPD_BIN_VERSION

/* Firmware image flags */
#define FWUPD_FLAG_SIGNED_PAYLOAD	(1 << 0)
#define FWUPD_FLAG_DUAL_IMAGE		(1 << 1)
#define FWUPD_FLAG_USABLE_DURING_UPDATE	(1 << 2)
#define FWUPD_FLAG_REMOVABLE		(1 << 3)

/* Canonical LVFS Device IDs & GUIDs in SIX */
#define FWUPD_DEVID_NVME		"SIX-NVME-SSD"
#define FWUPD_GUID_NVME			"b585990a-003e-5270-89d5-3705a17f9a43"

#define FWUPD_DEVID_UFS			"SIX-UFS-FLASH"
#define FWUPD_GUID_UFS			"e4a761c2-891b-5c34-a120-9f8e7d6c5b4a"
#define FWUPD_GUID_UFS_SAMSUNG		"41a057a2-a1ff-5764-bc6f-71aa045706ff"

#define FWUPD_DEVID_USB_SCSI		"SIX-USB-SCSI"
#define FWUPD_GUID_USB_SCSI		"7c3d2e1f-4b5a-5890-9123-456789abcdef"

/*
 * Microsoft Cabinet (MSCF v1.3) Archive Structures for LVFS .cab Packages
 */
struct fwupd_cab_hdr {
	unsigned int	signature;	/* FWUPD_CAB_MAGIC (0x4643534d "MSCF") */
	unsigned int	reserved1;
	unsigned int	cb_cabinet;	/* Total .cab file size in bytes */
	unsigned int	reserved2;
	unsigned int	coff_files;	/* File offset of first CFFILE entry */
	unsigned int	reserved3;
	unsigned char	version_minor;	/* 3 */
	unsigned char	version_major;	/* 1 */
	unsigned short	c_folders;	/* Number of CFFOLDER entries */
	unsigned short	c_files;	/* Number of CFFILE entries */
	unsigned short	flags;		/* Cabinet option flags (0x0004 = reserve present) */
	unsigned short	set_id;
	unsigned short	i_cabinet;
};

struct fwupd_cab_folder {
	unsigned int	coff_cab_start;	/* File offset of first CFDATA block */
	unsigned short	c_cfdata;	/* Number of CFDATA blocks in this folder */
	unsigned short	type_compress;	/* 0 = None (tcompTYPE_NONE) */
};

struct fwupd_cab_file_hdr {
	unsigned int	cb_file;	/* Uncompressed file size in bytes */
	unsigned int	uoff_folder_start; /* Uncompressed offset in folder */
	unsigned short	i_folder;	/* Folder index */
	unsigned short	date;
	unsigned short	time;
	unsigned short	attribs;
	/* Followed by null-terminated szName[] */
};

struct fwupd_cab_data_hdr {
	unsigned int	csum;		/* Checksum (0 = not computed) */
	unsigned short	cb_data;	/* Compressed/stored bytes in this block */
	unsigned short	cb_uncomp;	/* Uncompressed bytes in this block */
	/* Followed by cb_data payload bytes */
};

/*
 * 160-byte Controller Firmware Binary (.bin) Image Header
 * Starts with a valid 32-bit ARM Cortex-R5 branch vector (0xEA000026) followed
 * by the controller microcode descriptor and `code_len` bytes of microcode.
 */
struct fwupd_bin_hdr {
	unsigned int	arm_b_reset;	/* 0xEA000026 (ARM32 branch to offset 0xA0) */
	unsigned int	magic;		/* FWUPD_BIN_MAGIC (0x4d574653 "SFWM") */
	unsigned short	hdr_version;	/* FWUPD_BIN_VERSION (1) */
	unsigned short	flags;		/* FWUPD_FLAG_* bitmask */
	char		plugin[16];	/* Target plugin: "nvme", "ufs", or "scsi" */
	char		device_id[24];	/* Target HW ID: "SIX-NVME-SSD", "SIX-UFS-FLASH", "SIX-USB-SCSI" */
	char		guid[40];	/* LVFS AppStream GUID */
	char		fw_version[16];	/* Target firmware version: "1.4.2", "4.10", "1.10" */
	unsigned int	code_len;	/* Microcode body length in bytes following header */
	unsigned char	code_sha256[32];/* FIPS 180-4 SHA-256 digest of microcode body */
	unsigned int	release_ts;	/* Release Unix timestamp */
	unsigned char	rsvd[12];	/* Padding to 160 bytes */
};

#define fwupd_capsule_hdr fwupd_bin_hdr

/* SCSI WRITE_BUFFER (0x3B) / READ_BUFFER (0x3C) Modes (SPC-4) */
#define SCSI_WB_MODE_DATA			0x02
#define SCSI_WB_MODE_DOWNLOAD_SAVE		0x05
#define SCSI_WB_MODE_DOWNLOAD_OFFSET_SAVE	0x0E
#define SCSI_WB_MODE_ACTIVATE_DEFERRED		0x0F

/*
 * Streaming FIPS 180-4 SHA-256 implementation for arbitrary-sized .bin
 * firmware files in both kernel drivers and user-space fwupdmgr.
 */
struct fwupd_sha256_ctx {
	unsigned int	state[8];
	unsigned long	total_len;
	unsigned int	buf_len;
	unsigned char	buf[64];
};

static const unsigned int fwupd_sha256_k[64] = {
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

static inline unsigned int fwupd_rotr32(unsigned int x, int n)
{
	return (x >> n) | (x << (32 - n));
}

static inline void fwupd_sha256_block(unsigned int state[8], const unsigned char block[64])
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
		unsigned int s0 = fwupd_rotr32(w[i - 15], 7) ^ fwupd_rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
		unsigned int s1 = fwupd_rotr32(w[i - 2], 17) ^ fwupd_rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}

	a = state[0]; b = state[1]; c = state[2]; d = state[3];
	e = state[4]; f = state[5]; g = state[6]; h = state[7];

	for (i = 0; i < 64; i++) {
		unsigned int S1 = fwupd_rotr32(e, 6) ^ fwupd_rotr32(e, 11) ^ fwupd_rotr32(e, 25);
		unsigned int ch = (e & f) ^ ((~e) & g);
		unsigned int S0 = fwupd_rotr32(a, 2) ^ fwupd_rotr32(a, 13) ^ fwupd_rotr32(a, 22);
		unsigned int maj = (a & b) ^ (a & c) ^ (b & c);
		t1 = h + S1 + ch + fwupd_sha256_k[i] + w[i];
		t2 = S0 + maj;
		h = g; g = f; f = e; e = d + t1;
		d = c; c = b; b = a; a = t1 + t2;
	}

	state[0] += a; state[1] += b; state[2] += c; state[3] += d;
	state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

static inline void fwupd_sha256_init(struct fwupd_sha256_ctx *ctx)
{
	ctx->state[0] = 0x6a09e667U;
	ctx->state[1] = 0xbb67ae85U;
	ctx->state[2] = 0x3c6ef372U;
	ctx->state[3] = 0xa54ff53aU;
	ctx->state[4] = 0x510e527fU;
	ctx->state[5] = 0x9b05688cU;
	ctx->state[6] = 0x1f83d9abU;
	ctx->state[7] = 0x5be0cd19U;
	ctx->total_len = 0;
	ctx->buf_len = 0;
}

static inline void fwupd_sha256_update(struct fwupd_sha256_ctx *ctx,
				       const unsigned char *data,
				       unsigned int len)
{
	unsigned int i = 0;

	ctx->total_len += len;
	while (i < len) {
		ctx->buf[ctx->buf_len++] = data[i++];
		if (ctx->buf_len == 64) {
			fwupd_sha256_block(ctx->state, ctx->buf);
			ctx->buf_len = 0;
		}
	}
}

static inline void fwupd_sha256_final(struct fwupd_sha256_ctx *ctx,
				      unsigned char out_hash[32])
{
	unsigned char pad[64];
	unsigned int i, rem = ctx->buf_len;
	unsigned long bit_len_lo = ctx->total_len * 8UL;
	unsigned long bit_len_hi = (ctx->total_len >> 29);

	for (i = 0; i < 64; i++)
		pad[i] = 0;
	for (i = 0; i < rem; i++)
		pad[i] = ctx->buf[i];
	pad[rem] = 0x80;

	if (rem >= 56) {
		fwupd_sha256_block(ctx->state, pad);
		for (i = 0; i < 64; i++)
			pad[i] = 0;
	}

	pad[56] = (unsigned char)((bit_len_hi >> 24) & 0xff);
	pad[57] = (unsigned char)((bit_len_hi >> 16) & 0xff);
	pad[58] = (unsigned char)((bit_len_hi >> 8) & 0xff);
	pad[59] = (unsigned char)(bit_len_hi & 0xff);
	pad[60] = (unsigned char)((bit_len_lo >> 24) & 0xff);
	pad[61] = (unsigned char)((bit_len_lo >> 16) & 0xff);
	pad[62] = (unsigned char)((bit_len_lo >> 8) & 0xff);
	pad[63] = (unsigned char)(bit_len_lo & 0xff);
	fwupd_sha256_block(ctx->state, pad);

	for (i = 0; i < 8; i++) {
		out_hash[i * 4 + 0] = (unsigned char)((ctx->state[i] >> 24) & 0xff);
		out_hash[i * 4 + 1] = (unsigned char)((ctx->state[i] >> 16) & 0xff);
		out_hash[i * 4 + 2] = (unsigned char)((ctx->state[i] >> 8) & 0xff);
		out_hash[i * 4 + 3] = (unsigned char)(ctx->state[i] & 0xff);
	}
}

static inline void fwupd_sha256(const unsigned char *data, unsigned int len,
				unsigned char out_hash[32])
{
	struct fwupd_sha256_ctx ctx;
	fwupd_sha256_init(&ctx);
	fwupd_sha256_update(&ctx, data, len);
	fwupd_sha256_final(&ctx, out_hash);
}

/*
 * Generate deterministic controller microcode body for (device_id, fw_version)
 * and compute its 32-byte FIPS 180-4 SHA-256 digest.
 */
static inline void fwupd_build_microcode(const char *device_id, const char *fw_version,
					 unsigned char *payload_out, unsigned int len,
					 unsigned char code_sha256_out[32])
{
	unsigned int seed = 0x53495846U; /* "SIXF" */
	unsigned int i;
	const char *p;

	for (p = device_id; p && *p; p++)
		seed = (seed * 131U) ^ (unsigned char)(*p);
	for (p = fw_version; p && *p; p++)
		seed = (seed * 257U) + (unsigned char)(*p);

	for (i = 0; i < len; i++) {
		seed = seed * 1664525U + 1013904223U;
		payload_out[i] = (unsigned char)((seed >> 16) ^ (i & 0xff));
	}

	/* Embed ARM32 machine code prologue & microcode version string at offset 0 */
	if (len >= 64) {
		int pos = 0;
		const char *prefix = "SIX-MICROCODE:";
		for (p = prefix; *p && pos < 60; p++)
			payload_out[pos++] = (unsigned char)(*p);
		for (p = device_id; p && *p && pos < 60; p++)
			payload_out[pos++] = (unsigned char)(*p);
		payload_out[pos++] = ':';
		for (p = fw_version; p && *p && pos < 62; p++)
			payload_out[pos++] = (unsigned char)(*p);
		payload_out[pos] = '\0';
	}

	if (code_sha256_out)
		fwupd_sha256(payload_out, len, code_sha256_out);
}

/*
 * Build a complete 1184-byte controller .bin image (160B header + 1024B microcode)
 * and return the full .bin SHA-256 digest in `full_sha256_out`.
 */
static inline void fwupd_build_bin_image(const char *plugin,
					 const char *device_id,
					 const char *guid,
					 const char *fw_version,
					 unsigned short flags,
					 unsigned char *bin_out,
					 unsigned char full_sha256_out[32])
{
	struct fwupd_bin_hdr *hdr = (struct fwupd_bin_hdr *)bin_out;
	unsigned char *code = bin_out + sizeof(struct fwupd_bin_hdr);
	unsigned int i;
	const char *p;

	for (i = 0; i < sizeof(struct fwupd_bin_hdr) + FWUPD_DEFAULT_PAYLOAD_SIZE; i++)
		bin_out[i] = 0;

	hdr->arm_b_reset = FWUPD_ARM_RESET_VECTOR;
	hdr->magic = FWUPD_BIN_MAGIC;
	hdr->hdr_version = FWUPD_BIN_VERSION;
	hdr->flags = flags;

	for (i = 0, p = plugin; p && *p && i < sizeof(hdr->plugin) - 1; i++, p++)
		hdr->plugin[i] = *p;
	for (i = 0, p = device_id; p && *p && i < sizeof(hdr->device_id) - 1; i++, p++)
		hdr->device_id[i] = *p;
	for (i = 0, p = guid; p && *p && i < sizeof(hdr->guid) - 1; i++, p++)
		hdr->guid[i] = *p;
	for (i = 0, p = fw_version; p && *p && i < sizeof(hdr->fw_version) - 1; i++, p++)
		hdr->fw_version[i] = *p;

	hdr->code_len = FWUPD_DEFAULT_PAYLOAD_SIZE;
	hdr->release_ts = 1790517600U;

	fwupd_build_microcode(device_id, fw_version, code,
			      FWUPD_DEFAULT_PAYLOAD_SIZE, hdr->code_sha256);

	if (full_sha256_out) {
		fwupd_sha256(bin_out,
			     sizeof(struct fwupd_bin_hdr) + FWUPD_DEFAULT_PAYLOAD_SIZE,
			     full_sha256_out);
	}
}

/*
 * Extract or synthesize a firmware revision string from any .bin image header:
 *   1. If `hdr_buf` starts with a `struct fwupd_bin_hdr` (magic == FWUPD_BIN_MAGIC),
 *      returns `hdr->fw_version`.
 *   2. Otherwise scans the first `hdr_len` bytes of the real-world .bin file for
 *      an embedded ASCII semantic version string (e.g. "1.4.3", "2.10", "v3.01").
 *   3. If the binary is purely opaque machine code with no ASCII version banner,
 *      derives a deterministic revision string from its SHA-256 digest.
 */
static inline void fwupd_detect_bin_version(const unsigned char *hdr_buf,
					    unsigned int hdr_len,
					    const unsigned char sha256[32],
					    int max_ver_len,
					    char *out_ver)
{
	unsigned int i;

	if (hdr_len >= sizeof(struct fwupd_bin_hdr)) {
		const struct fwupd_bin_hdr *hdr = (const struct fwupd_bin_hdr *)hdr_buf;
		if (hdr->magic == FWUPD_BIN_MAGIC && hdr->fw_version[0]) {
			int k = 0;
			while (hdr->fw_version[k] && k < max_ver_len) {
				out_ver[k] = hdr->fw_version[k];
				k++;
			}
			out_ver[k] = '\0';
			return;
		}
	}

	/* Samsung JEDEC UFS Firmware Header ("UFSH"): BCD version at 0x0e (Pxx) & 0x0c (FWxx) */
	if (hdr_len >= 16 &&
	    hdr_buf[0] == 'U' && hdr_buf[1] == 'F' &&
	    hdr_buf[2] == 'S' && hdr_buf[3] == 'H') {
		unsigned char b_maj = hdr_buf[0x0e];
		unsigned char b_min = hdr_buf[0x0c];
		if (max_ver_len >= 4 && (b_maj != 0 || b_min != 0)) {
			out_ver[0] = (char)('0' + ((b_maj >> 4) & 0x0f));
			out_ver[1] = (char)('0' + (b_maj & 0x0f));
			out_ver[2] = (char)('0' + ((b_min >> 4) & 0x0f));
			out_ver[3] = (char)('0' + (b_min & 0x0f));
			out_ver[4] = '\0';
			return;
		}
	}

	/* Scan binary header for an embedded ASCII semantic version (D.D or D.D.D) */
	for (i = 0; i + 3 < hdr_len; i++) {
		if (hdr_buf[i] >= '0' && hdr_buf[i] <= '9' &&
		    hdr_buf[i + 1] == '.' &&
		    hdr_buf[i + 2] >= '0' && hdr_buf[i + 2] <= '9') {
			int k = 0;
			unsigned int j = i;
			while (j < hdr_len && k < max_ver_len &&
			       ((hdr_buf[j] >= '0' && hdr_buf[j] <= '9') || hdr_buf[j] == '.')) {
				out_ver[k++] = (char)hdr_buf[j++];
			}
			if (k >= 3 && out_ver[k - 1] != '.') {
				out_ver[k] = '\0';
				return;
			}
		}
	}

	/* Fallback for opaque raw binary blobs: deterministic revision from SHA-256 */
	if (max_ver_len >= 6) {
		out_ver[0] = '2';
		out_ver[1] = '.';
		out_ver[2] = (char)('0' + (sha256[0] % 10));
		out_ver[3] = '.';
		out_ver[4] = (char)('0' + (sha256[1] % 10));
		out_ver[5] = '\0';
	} else {
		out_ver[0] = '2';
		out_ver[1] = '.';
		out_ver[2] = (char)('0' + (sha256[0] % 10));
		out_ver[3] = (char)('0' + (sha256[1] % 10));
		out_ver[4] = '\0';
	}
}

/*
 * Controller-side streaming .bin firmware receiver state.
 * Handles arbitrary-sized .bin files without fixed staging buffer limits.
 */
struct fwupd_stream_state {
	unsigned char		hdr_buf[FWUPD_HDR_INSPECT_LEN];
	unsigned int		hdr_captured;
	unsigned int		staged_len;
	unsigned int		nonzero_bytes;
	struct fwupd_sha256_ctx	full_sha;
	struct fwupd_sha256_ctx	code_sha;
};

static inline int fwupd_stream_write_chunk(struct fwupd_stream_state *st,
					   unsigned int offset,
					   const unsigned char *data,
					   unsigned int len)
{
	unsigned int i;

	if (!st || !data || len == 0)
		return -1;

	if (offset == 0) {
		for (i = 0; i < FWUPD_HDR_INSPECT_LEN; i++)
			st->hdr_buf[i] = 0;
		st->hdr_captured = 0;
		st->staged_len = 0;
		st->nonzero_bytes = 0;
		fwupd_sha256_init(&st->full_sha);
		fwupd_sha256_init(&st->code_sha);
	} else if (offset != st->staged_len) {
		return -1;
	}

	for (i = 0; i < len; i++) {
		unsigned int pos = offset + i;
		if (pos < FWUPD_HDR_INSPECT_LEN) {
			st->hdr_buf[pos] = data[i];
			if (pos + 1 > st->hdr_captured)
				st->hdr_captured = pos + 1;
		}
		if (data[i] != 0)
			st->nonzero_bytes++;
	}

	fwupd_sha256_update(&st->full_sha, data, len);

	if (offset + len > (unsigned int)sizeof(struct fwupd_bin_hdr)) {
		unsigned int hdr_sz = (unsigned int)sizeof(struct fwupd_bin_hdr);
		unsigned int code_start = (offset >= hdr_sz) ? 0U : (hdr_sz - offset);
		fwupd_sha256_update(&st->code_sha, data + code_start, len - code_start);
	}

	st->staged_len += len;
	return 0;
}

static inline int fwupd_stream_validate_and_finalize(struct fwupd_stream_state *st,
						     const char *expected_device_id,
						     int max_ver_len,
						     char *out_ver,
						     unsigned char out_full_sha256[32])
{
	unsigned int i;

	if (!st || st->staged_len < 64 || st->nonzero_bytes < 16)
		return -1;

	fwupd_sha256_final(&st->full_sha, out_full_sha256);

	if (st->hdr_captured >= (unsigned int)sizeof(struct fwupd_bin_hdr)) {
		const struct fwupd_bin_hdr *hdr = (const struct fwupd_bin_hdr *)st->hdr_buf;
		if (hdr->magic == FWUPD_BIN_MAGIC) {
			unsigned char calc_code_sha[32];
			int id_match = 1;

			for (i = 0; expected_device_id[i] || hdr->device_id[i]; i++) {
				if (expected_device_id[i] != hdr->device_id[i]) {
					id_match = 0;
					break;
				}
			}
			if (!id_match || hdr->code_len == 0 ||
			    sizeof(struct fwupd_bin_hdr) + hdr->code_len != st->staged_len)
				return -1;

			fwupd_sha256_final(&st->code_sha, calc_code_sha);
			for (i = 0; i < 32; i++) {
				if (calc_code_sha[i] != hdr->code_sha256[i])
					return -1;
			}
			fwupd_detect_bin_version(st->hdr_buf, st->hdr_captured,
						 out_full_sha256, max_ver_len, out_ver);
			return 0;
		}
	}

	/* Arbitrary real-world vendor .bin firmware image */
	fwupd_detect_bin_version(st->hdr_buf, st->hdr_captured,
				 out_full_sha256, max_ver_len, out_ver);
	return 0;
}

#endif /* _LINUX_FWUPD_H */
