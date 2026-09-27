/*
 * include/linux/fwupd.h
 *
 * Firmware Update (fwupd / LVFS) Capsule Binary Format, Cryptographic
 * Signature Header, FIPS 180-4 SHA-256 Engine, and Plugin Protocol
 * Definitions for SIX.
 *
 * Used by:
 *   - Kernel NVMe 1.4 driver (drivers/block/nvme.c):
 *       NVME_ADMIN_DOWNLOAD_FW (0x11) & NVME_ADMIN_ACTIVATE_FW (0x10)
 *   - Kernel JEDEC UFS 4.0 driver (drivers/block/ufshcd.c):
 *       SCSI WRITE_BUFFER (0x3B, Mode 0x0E/0x0F) & READ_BUFFER (0x3C)
 *   - Kernel USB Mass Storage SCSI driver (drivers/block/usb_sd.c):
 *       SCSI INQUIRY (0x12), WRITE_BUFFER (0x3B) & READ_BUFFER (0x3C)
 *   - User-space fwupd manager (/bin/fwupdmgr)
 */

#ifndef _LINUX_FWUPD_H
#define _LINUX_FWUPD_H

#define FWUPD_CAPSULE_MAGIC		0x50555746U	/* "FWUP" */
#define FWUPD_CAPSULE_VERSION		1
#define FWUPD_DEFAULT_PAYLOAD_SIZE	1024
#define FWUPD_MAX_PAYLOAD_SIZE		4096
#define FWUPD_MAX_CAPSULE_SIZE		(sizeof(struct fwupd_capsule_hdr) + FWUPD_MAX_PAYLOAD_SIZE)
#define FWUPD_CHUNK_SIZE		512

/* Capsule flags */
#define FWUPD_FLAG_SIGNED_PAYLOAD	(1 << 0)
#define FWUPD_FLAG_DUAL_IMAGE		(1 << 1)
#define FWUPD_FLAG_USABLE_DURING_UPDATE	(1 << 2)
#define FWUPD_FLAG_REMOVABLE		(1 << 3)

/* Canonical LVFS Device IDs & GUIDs in SIX */
#define FWUPD_DEVID_NVME		"SIX-NVME-SSD"
#define FWUPD_GUID_NVME			"b585990a-003e-5270-89d5-3705a17f9a43"

#define FWUPD_DEVID_UFS			"SIX-UFS-FLASH"
#define FWUPD_GUID_UFS			"e4a761c2-891b-5c34-a120-9f8e7d6c5b4a"

#define FWUPD_DEVID_USB_SCSI		"SIX-USB-SCSI"
#define FWUPD_GUID_USB_SCSI		"7c3d2e1f-4b5a-5890-9123-456789abcdef"

/*
 * 160-byte Signed Firmware Update Capsule Header (.fw)
 * Followed immediately by `payload_len` bytes of target microcode.
 */
struct fwupd_capsule_hdr {
	unsigned int	magic;		/* FWUPD_CAPSULE_MAGIC (0x50555746) */
	unsigned short	hdr_version;	/* FWUPD_CAPSULE_VERSION (1) */
	unsigned short	flags;		/* FWUPD_FLAG_* bitmask */
	char		plugin[16];	/* Target plugin: "nvme" or "scsi" */
	char		device_id[24];	/* Target HW ID: "SIX-NVME-SSD", "SIX-UFS-FLASH", "SIX-USB-SCSI" */
	char		guid[40];	/* LVFS AppStream GUID */
	char		fw_version[16];	/* Target firmware version: "1.4.2", "4.10", "1.10" */
	unsigned int	payload_len;	/* Microcode payload length in bytes */
	unsigned char	sha256[32];	/* FIPS 180-4 SHA-256 digest of microcode payload */
	unsigned int	release_ts;	/* Release Unix timestamp */
	unsigned char	rsvd[16];	/* Reserved padding (total 160 bytes) */
};

/* SCSI WRITE_BUFFER (0x3B) / READ_BUFFER (0x3C) Modes (SPC-4) */
#define SCSI_WB_MODE_DATA			0x02
#define SCSI_WB_MODE_DOWNLOAD_SAVE		0x05
#define SCSI_WB_MODE_DOWNLOAD_OFFSET_SAVE	0x0E
#define SCSI_WB_MODE_ACTIVATE_DEFERRED		0x0F

/*
 * Self-contained FIPS 180-4 SHA-256 implementation for firmware capsule
 * authentication in both kernel drivers and user-space fwupdmgr.
 */
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

static inline void fwupd_sha256(const unsigned char *data, unsigned int len,
				unsigned char out_hash[32])
{
	unsigned int state[8] = {
		0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
		0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
	};
	unsigned char pad[64];
	unsigned int offset = 0;
	unsigned int rem, i;
	unsigned long bit_len = (unsigned long)len * 8UL;

	while (offset + 64 <= len) {
		fwupd_sha256_block(state, data + offset);
		offset += 64;
	}

	rem = len - offset;
	for (i = 0; i < 64; i++)
		pad[i] = 0;
	for (i = 0; i < rem; i++)
		pad[i] = data[offset + i];
	pad[rem] = 0x80;

	if (rem >= 56) {
		fwupd_sha256_block(state, pad);
		for (i = 0; i < 64; i++)
			pad[i] = 0;
	}

	pad[60] = (unsigned char)((bit_len >> 24) & 0xff);
	pad[61] = (unsigned char)((bit_len >> 16) & 0xff);
	pad[62] = (unsigned char)((bit_len >> 8) & 0xff);
	pad[63] = (unsigned char)(bit_len & 0xff);
	fwupd_sha256_block(state, pad);

	for (i = 0; i < 8; i++) {
		out_hash[i * 4 + 0] = (unsigned char)((state[i] >> 24) & 0xff);
		out_hash[i * 4 + 1] = (unsigned char)((state[i] >> 16) & 0xff);
		out_hash[i * 4 + 2] = (unsigned char)((state[i] >> 8) & 0xff);
		out_hash[i * 4 + 3] = (unsigned char)(state[i] & 0xff);
	}
}

/*
 * Generate deterministic controller microcode image for (device_id, fw_version)
 * and compute its 32-byte FIPS 180-4 SHA-256 digest.
 */
static inline void fwupd_build_microcode(const char *device_id, const char *fw_version,
					 unsigned char *payload_out, unsigned int len,
					 unsigned char sha256_out[32])
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

	/* Embed human-readable microcode signature banner at offset 0 */
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

	if (sha256_out)
		fwupd_sha256(payload_out, len, sha256_out);
}

#endif /* _LINUX_FWUPD_H */
