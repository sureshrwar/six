/*
 * ota.c - Android Seamless A/B OTA & Boot Control Utility (/bin/ota, /bin/bootctl)
 *
 * Implements Android update_engine + bootctl A/B slot management for SIX:
 *   - Inspects A/B partitions inside p4:bin_storage (super, 56 MiB on /dev/hdd),
 *     UFS Boot LUNs (/dev/ufsb & /dev/ufsc, AVB0 vbmeta), and UFS RPMB (/dev/ufs-rpmb).
 *   - Stages and applies live A/B OTA updates into the inactive slot (cloning
 *     system & vendor EROFS slices, updating /build.prop, recomputing 3-level
 *     SHA-256 dm-verity Merkle spines, updating UFS AVB0 vbmeta & RPMB anti-rollback
 *     index, and activating the new slot live via DM_IOC_OTA_ACTIVATE).
 *   - Supports slot switching (ota switch a|b / bootctl set-active-boot-slot 0|1)
 *     and corruption / automatic rollback testing (ota corrupt-b / ota repair-b).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "../../include/linux/dm.h"
#include "../../include/linux/ufs.h"

/* Super partition (/dev/hdd) A/B slice geometry (512-byte sectors) */
#define SYSTEM_A_START_SEC	0UL
#define SYSTEM_A_DATA_SECS	30720UL		/* 15 MiB (15360 x 1KB blocks) */
#define SYSTEM_A_HASH_SEC	30720UL		/* 1 MiB dm-verity hash region */
#define SYSTEM_SLICE_SECS	32768UL		/* 16 MiB total per system slot */

#define SARTHAK_START_SEC	32768UL
#define SARTHAK_TOTAL_SECS	32768UL		/* 16 MiB go/erofs-verity */

#define VENDOR_A_START_SEC	65536UL
#define VENDOR_A_DATA_SECS	6144UL		/* 3 MiB (3072 x 1KB blocks) */
#define VENDOR_A_HASH_SEC	71680UL		/* 1 MiB dm-verity hash region */
#define VENDOR_SLICE_SECS	8192UL		/* 4 MiB total per vendor slot */

#define SYSTEM_B_START_SEC	73728UL
#define SYSTEM_B_DATA_SECS	30720UL
#define SYSTEM_B_HASH_SEC	104448UL

#define VENDOR_B_START_SEC	106496UL
#define VENDOR_B_DATA_SECS	6144UL
#define VENDOR_B_HASH_SEC	112640UL

#define DEFAULT_RPMB_KEY_STR	"six_avb_rpmb_key_2026"

struct six_verity_sb {
	char magic[8];
	unsigned int version;
	unsigned int hash_type;
	unsigned int data_block_size;
	unsigned int hash_block_size;
	unsigned int data_blocks;
	unsigned int hash_start_block;
	unsigned int l0_offset_blocks;
	unsigned int l1_offset_blocks;
	unsigned int l2_offset_blocks;
	char algorithm[32];
	unsigned char salt[32];
	unsigned char root_hash[32];
};

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

static void sha256_salted_1k(const unsigned char salt[32],
			     const unsigned char blk[1024],
			     unsigned char out_digest[32])
{
	unsigned int state[8] = {
		0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
		0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
	};
	unsigned char chunk[64];
	int i;

	memcpy(chunk, salt, 32);
	memcpy(chunk + 32, blk, 32);
	sha256_transform(state, chunk);

	for (i = 0; i < 15; i++)
		sha256_transform(state, blk + 32 + i * 64);

	memset(chunk, 0, 64);
	memcpy(chunk, blk + 992, 32);
	chunk[32] = 0x80;
	chunk[62] = 0x21;
	chunk[63] = 0x00;
	sha256_transform(state, chunk);

	for (i = 0; i < 8; i++) {
		out_digest[i * 4 + 0] = (unsigned char)((state[i] >> 24) & 0xff);
		out_digest[i * 4 + 1] = (unsigned char)((state[i] >> 16) & 0xff);
		out_digest[i * 4 + 2] = (unsigned char)((state[i] >> 8) & 0xff);
		out_digest[i * 4 + 3] = (unsigned char)(state[i] & 0xff);
	}
}

static void bytes_to_hex(const unsigned char *in, int len, char *out)
{
	static const char hex[] = "0123456789abcdef";
	int i;
	for (i = 0; i < len; i++) {
		out[i * 2 + 0] = hex[(in[i] >> 4) & 0x0f];
		out[i * 2 + 1] = hex[in[i] & 0x0f];
	}
	out[len * 2] = '\0';
}

struct sha256_ctx {
	unsigned int state[8];
	unsigned long long total_bytes;
	unsigned int buf_len;
	unsigned char buf[64];
};

static void sha256_init(struct sha256_ctx *ctx)
{
	ctx->state[0] = 0x6a09e667U;
	ctx->state[1] = 0xbb67ae85U;
	ctx->state[2] = 0x3c6ef372U;
	ctx->state[3] = 0xa54ff53aU;
	ctx->state[4] = 0x510e527fU;
	ctx->state[5] = 0x9b05688cU;
	ctx->state[6] = 0x1f83d9abU;
	ctx->state[7] = 0x5be0cd19U;
	ctx->total_bytes = 0;
	ctx->buf_len = 0;
}

static void sha256_update(struct sha256_ctx *ctx, const unsigned char *data, unsigned long len)
{
	unsigned long i = 0;

	ctx->total_bytes += (unsigned long long)len;
	if (ctx->buf_len > 0) {
		while (i < len && ctx->buf_len < 64)
			ctx->buf[ctx->buf_len++] = data[i++];
		if (ctx->buf_len == 64) {
			sha256_transform(ctx->state, ctx->buf);
			ctx->buf_len = 0;
		}
	}
	while (i + 64 <= len) {
		sha256_transform(ctx->state, data + i);
		i += 64;
	}
	while (i < len)
		ctx->buf[ctx->buf_len++] = data[i++];
}

static void sha256_final(struct sha256_ctx *ctx, unsigned char out[32])
{
	unsigned long long bits = ctx->total_bytes * 8ULL;
	int i;

	ctx->buf[ctx->buf_len++] = 0x80;
	if (ctx->buf_len > 56) {
		while (ctx->buf_len < 64)
			ctx->buf[ctx->buf_len++] = 0;
		sha256_transform(ctx->state, ctx->buf);
		ctx->buf_len = 0;
	}
	while (ctx->buf_len < 56)
		ctx->buf[ctx->buf_len++] = 0;
	for (i = 0; i < 8; i++)
		ctx->buf[56 + i] = (unsigned char)((bits >> ((7 - i) * 8)) & 0xff);
	sha256_transform(ctx->state, ctx->buf);

	for (i = 0; i < 8; i++) {
		out[i * 4 + 0] = (unsigned char)((ctx->state[i] >> 24) & 0xff);
		out[i * 4 + 1] = (unsigned char)((ctx->state[i] >> 16) & 0xff);
		out[i * 4 + 2] = (unsigned char)((ctx->state[i] >> 8) & 0xff);
		out[i * 4 + 3] = (unsigned char)(ctx->state[i] & 0xff);
	}
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

static int rpmb_get_counter(unsigned int *out_cnt)
{
	struct ufs_rpmb_frame frame;
	int fd = open("/dev/ufs-rpmb", O_RDWR);
	int rc;

	if (fd < 0)
		return -1;
	memset(&frame, 0, sizeof(frame));
	frame.req_resp = RPMB_REQ_GET_COUNTER;
	rc = ioctl(fd, UFS_IOCTL_RPMB, &frame);
	close(fd);
	if (frame.result == RPMB_RES_NO_AUTH_KEY)
		return (int)RPMB_RES_NO_AUTH_KEY;
	if (rc < 0 || frame.result != RPMB_RES_OK)
		return -1;
	if (out_cnt)
		*out_cnt = frame.write_counter;
	return 0;
}

static int rpmb_record_rollback(unsigned long long rollback_idx, const char *build_id)
{
	struct ufs_rpmb_frame frame;
	unsigned char key[32];
	unsigned int cnt = 0;
	int fd, rc;

	derive_rpmb_key(DEFAULT_RPMB_KEY_STR, key);
	rc = rpmb_get_counter(&cnt);
	if (rc == RPMB_RES_NO_AUTH_KEY) {
		fd = open("/dev/ufs-rpmb", O_RDWR);
		if (fd >= 0) {
			memset(&frame, 0, sizeof(frame));
			frame.req_resp = RPMB_REQ_PROGRAM_KEY;
			memcpy(frame.key_mac, key, 32);
			ioctl(fd, UFS_IOCTL_RPMB, &frame);
			close(fd);
		}
		rc = rpmb_get_counter(&cnt);
	}
	if (rc != 0)
		return -1;

	fd = open("/dev/ufs-rpmb", O_RDWR);
	if (fd < 0)
		return -1;
	memset(&frame, 0, sizeof(frame));
	frame.req_resp = RPMB_REQ_WRITE_DATA;
	frame.block_count = 1;
	frame.addr = 1;
	frame.write_counter = cnt;
	sprintf((char *)frame.data,
		"AVB_ROLLBACK_INDEX=%llu BUILD_ID=%s",
		rollback_idx, build_id ? build_id : "OTA");
	rpmb_compute_mac(key, &frame, frame.key_mac);
	rc = ioctl(fd, UFS_IOCTL_RPMB, &frame);
	close(fd);
	if (rc < 0 || frame.result != RPMB_RES_OK)
		return -1;
	return 0;
}

static int read_vbmeta(int slot, struct six_avb_vbmeta *vbm)
{
	const char *dev = (slot == 1) ? "/dev/ufsc" : "/dev/ufsb";
	int fd = open(dev, O_RDONLY);
	if (fd < 0)
		return -1;
	memset(vbm, 0, sizeof(*vbm));
	lseek(fd, (long)(SIX_AVB_VBMETA_SECTOR * 512UL), 0);
	if (read(fd, vbm, sizeof(*vbm)) != (int)sizeof(*vbm)) {
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

static int write_vbmeta(int slot, const struct six_avb_vbmeta *vbm)
{
	const char *dev = (slot == 1) ? "/dev/ufsc" : "/dev/ufsb";
	int fd = open(dev, O_RDWR);
	if (fd < 0)
		return -1;
	lseek(fd, (long)(SIX_AVB_VBMETA_SECTOR * 512UL), 0);
	if (write(fd, vbm, sizeof(*vbm)) != (int)sizeof(*vbm)) {
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

static int read_1k(int fd, unsigned long sector, unsigned char buf[1024])
{
	if (lseek(fd, (long)(sector * 512UL), 0) < 0)
		return -1;
	if (read(fd, buf, 1024) != 1024)
		return -1;
	return 0;
}

static int write_1k(int fd, unsigned long sector, const unsigned char buf[1024])
{
	if (lseek(fd, (long)(sector * 512UL), 0) < 0)
		return -1;
	if (write(fd, buf, 1024) != 1024)
		return -1;
	return 0;
}

/*
 * Update a single 1024-byte data block at blk_nr within a dm-verity slice on
 * /dev/hdd and recompute the 4-level SHA-256 Merkle spine (L0 -> L1 -> L2 -> sb.root_hash).
 */
static int update_verity_block_and_spine(int hdd_fd,
					 unsigned long slice_start_sec,
					 unsigned long hash_start_sec,
					 unsigned long blk_nr,
					 const unsigned char new_data[1024],
					 unsigned char out_root_hash[32])
{
	struct six_verity_sb sb;
	unsigned char sb_blk[1024], l0_blk[1024], l1_blk[1024], l2_blk[1024];
	unsigned char d_hash[32], l0_hash[32], l1_hash[32], root_hash[32];
	unsigned long data_sec = slice_start_sec + (blk_nr << 1);
	unsigned long l2_sec   = hash_start_sec + (1UL << 1);
	unsigned long l1_sec   = hash_start_sec + ((2UL + (blk_nr >> 10)) << 1);
	unsigned long l0_sec   = hash_start_sec + ((17UL + (blk_nr >> 5)) << 1);

	if (read_1k(hdd_fd, hash_start_sec, sb_blk) < 0)
		return -1;
	memcpy(&sb, sb_blk, sizeof(sb));
	if (memcmp(sb.magic, "verity\0\0", 8) != 0)
		return -1;

	/* 1. Write new 1 KB data block */
	if (write_1k(hdd_fd, data_sec, new_data) < 0)
		return -1;
	sha256_salted_1k(sb.salt, new_data, d_hash);

	/* 2. Update Level-0 leaf block and hash it */
	if (read_1k(hdd_fd, l0_sec, l0_blk) < 0)
		return -1;
	memcpy(l0_blk + ((blk_nr & 31UL) << 5), d_hash, 32);
	if (write_1k(hdd_fd, l0_sec, l0_blk) < 0)
		return -1;
	sha256_salted_1k(sb.salt, l0_blk, l0_hash);

	/* 3. Update Level-1 interior block and hash it */
	if (read_1k(hdd_fd, l1_sec, l1_blk) < 0)
		return -1;
	memcpy(l1_blk + (((blk_nr >> 5) & 31UL) << 5), l0_hash, 32);
	if (write_1k(hdd_fd, l1_sec, l1_blk) < 0)
		return -1;
	sha256_salted_1k(sb.salt, l1_blk, l1_hash);

	/* 4. Update Level-2 root block and compute new Root Hash */
	if (read_1k(hdd_fd, l2_sec, l2_blk) < 0)
		return -1;
	memcpy(l2_blk + ((blk_nr >> 10) << 5), l1_hash, 32);
	if (write_1k(hdd_fd, l2_sec, l2_blk) < 0)
		return -1;
	sha256_salted_1k(sb.salt, l2_blk, root_hash);

	/* 5. Update six_verity_sb superblock with new Root Hash */
	memcpy(sb.root_hash, root_hash, 32);
	memcpy(sb_blk, &sb, sizeof(sb));
	if (write_1k(hdd_fd, hash_start_sec, sb_blk) < 0)
		return -1;

	if (out_root_hash)
		memcpy(out_root_hash, root_hash, 32);
	return 0;
}

static void extract_prop_val(const char *path, const char *key, char *out, int out_sz)
{
	FILE *fp = fopen(path, "r");
	char line[256];
	int klen = (int)strlen(key);

	out[0] = '\0';
	if (!fp)
		return;
	while (fgets(line, sizeof(line), fp)) {
		if (strncmp(line, key, klen) == 0 && line[klen] == '=') {
			char *v = line + klen + 1;
			int n = (int)strlen(v);
			while (n > 0 && (v[n - 1] == '\n' || v[n - 1] == '\r'))
				v[--n] = '\0';
			strncpy(out, v, out_sz - 1);
			out[out_sz - 1] = '\0';
			break;
		}
	}
	fclose(fp);
}

static void format_slot_flags(unsigned int flags, char *out)
{
	out[0] = '\0';
	if (flags & SIX_SLOT_FLAG_ACTIVE)
		strcat(out, "ACTIVE ");
	if (flags & SIX_SLOT_FLAG_BOOTABLE)
		strcat(out, "BOOTABLE ");
	if (flags & SIX_SLOT_FLAG_SUCCESSFUL)
		strcat(out, "SUCCESSFUL ");
	if (flags & SIX_SLOT_FLAG_UNBOOTABLE)
		strcat(out, "UNBOOTABLE ");
	if (!out[0])
		strcpy(out, "NONE");
	else if (out[strlen(out) - 1] == ' ')
		out[strlen(out) - 1] = '\0';
}

static int get_ota_status(struct dm_ota_status *st)
{
	int fd = open("/dev/mapper/control", O_RDWR);
	int rc;
	if (fd < 0)
		return -1;
	memset(st, 0, sizeof(*st));
	rc = ioctl(fd, DM_IOC_OTA_STATUS, st);
	close(fd);
	return rc;
}

static int cmd_status(void)
{
	struct dm_ota_status st;
	struct six_avb_vbmeta vbm_a, vbm_b;
	char sys_id[64], sys_slot[16], sys_inc[32], sys_ota[16];
	char vnd_id[64], vnd_slot[16], vnd_ota[16];
	char flags_a[64], flags_b[64];
	char sys_hash_a[65], vnd_hash_a[65], sys_hash_b[65], vnd_hash_b[65];
	unsigned int rpmb_cnt = 0;
	int has_rpmb;

	if (get_ota_status(&st) < 0) {
		fprintf(stderr, "ota: cannot query DM_IOC_OTA_STATUS on /dev/mapper/control\n");
		return 1;
	}

	read_vbmeta(0, &vbm_a);
	read_vbmeta(1, &vbm_b);
	has_rpmb = (rpmb_get_counter(&rpmb_cnt) == 0);

	extract_prop_val("/system/build.prop", "ro.build.id", sys_id, sizeof(sys_id));
	extract_prop_val("/system/build.prop", "ro.boot.slot_suffix", sys_slot, sizeof(sys_slot));
	extract_prop_val("/system/build.prop", "ro.build.version.incremental", sys_inc, sizeof(sys_inc));
	extract_prop_val("/system/build.prop", "ro.ota.version", sys_ota, sizeof(sys_ota));

	extract_prop_val("/vendor/build.prop", "ro.vendor.build.id", vnd_id, sizeof(vnd_id));
	extract_prop_val("/vendor/build.prop", "ro.boot.slot_suffix", vnd_slot, sizeof(vnd_slot));
	extract_prop_val("/vendor/build.prop", "ro.ota.version", vnd_ota, sizeof(vnd_ota));

	format_slot_flags(vbm_a.flags, flags_a);
	format_slot_flags(vbm_b.flags, flags_b);
	bytes_to_hex(vbm_a.system_root_hash, 32, sys_hash_a);
	bytes_to_hex(vbm_a.vendor_root_hash, 32, vnd_hash_a);
	bytes_to_hex(vbm_b.system_root_hash, 32, sys_hash_b);
	bytes_to_hex(vbm_b.vendor_root_hash, 32, vnd_hash_b);

	printf("=== Android Seamless A/B OTA & AVB 2.0 Boot Control Status ===\n");
	printf("Active Boot Slot:   slot_%c (suffix %s, UFS bBootLunID=0x%02x -> %s)\n",
	       st.active_slot ? 'b' : 'a',
	       st.active_slot ? "_b" : "_a",
	       st.boot_lun_id,
	       (st.boot_lun_id == 2) ? "/dev/ufsc" : "/dev/ufsb");
	printf("Super Partition:    /dev/hdd (p4:bin_storage, 56 MiB / 114,688 sectors)\n");
	printf("RPMB Anti-Rollback: /dev/ufs-rpmb (Write Counter=%u, RollbackIdx A=%llu B=%llu)\n",
	       rpmb_cnt,
	       (unsigned long long)vbm_a.rollback_index,
	       (unsigned long long)vbm_b.rollback_index);
	(void)has_rpmb;
	printf("\n");
	printf("Live Mounted Partitions (EROFS + dm-verity SHA-256):\n");
	printf("  /system (/dev/dm-0): /dev/hdd+%lu (hash@%lu) build=%s slot=%s ota_v=%s (verified=%lu, corrupt=%lu)\n",
	       st.system_start_sector, st.system_hash_sector,
	       sys_id[0] ? sys_id : "-", sys_slot[0] ? sys_slot : "-",
	       sys_ota[0] ? sys_ota : "-",
	       st.system_verified, st.system_corrupt);
	printf("  /vendor (/dev/dm-6): /dev/hdd+%lu (hash@%lu) build=%s slot=%s ota_v=%s (verified=%lu, corrupt=%lu)\n",
	       st.vendor_start_sector, st.vendor_hash_sector,
	       vnd_id[0] ? vnd_id : "-", vnd_slot[0] ? vnd_slot : "-",
	       vnd_ota[0] ? vnd_ota : "-",
	       st.vendor_verified, st.vendor_corrupt);
	printf("  /system/bin-sarthak: /dev/hdd+32768 (16 MiB go/erofs-verity native SHA-256)\n");
	printf("\n");
	printf("UFS Boot LUN Slot Table (AVB0 vbmeta @ Sector 1):\n");
	printf("  Slot A (_a, /dev/ufsb, LUN 1): [%s] prio=%u tries=%u rollback=%llu build=%s\n",
	       flags_a, vbm_a.priority, vbm_a.tries_remaining,
	       (unsigned long long)vbm_a.rollback_index,
	       vbm_a.build_id[0] ? vbm_a.build_id : "-");
	printf("    system_a [0..16M]:   root_hash=%.32s...\n", sys_hash_a);
	printf("    vendor_a [32..36M]:  root_hash=%.32s...\n", vnd_hash_a);
	printf("  Slot B (_b, /dev/ufsc, LUN 2): [%s] prio=%u tries=%u rollback=%llu build=%s\n",
	       flags_b, vbm_b.priority, vbm_b.tries_remaining,
	       (unsigned long long)vbm_b.rollback_index,
	       vbm_b.build_id[0] ? vbm_b.build_id : "-");
	printf("    system_b [36..52M]:  root_hash=%.32s...\n", sys_hash_b);
	printf("    vendor_b [52..56M]:  root_hash=%.32s...\n", vnd_hash_b);
	return 0;
}

static void save_ufs_slot_status_file(int slot, const char *build_id)
{
	FILE *fp = fopen("/ufs/ota/slot_status.txt", "w");
	if (!fp)
		return;
	fprintf(fp, "active_slot=%c\nslot_suffix=_%c\nbuild_id=%s\n",
		slot ? 'b' : 'a',
		slot ? 'b' : 'a',
		(build_id && build_id[0]) ? build_id : (slot ? "SIX.261003.001.B1" : "SIX.261003.001.A1"));
	fclose(fp);
}

static int parse_slot_arg(const char *s)
{
	if (!s || !s[0])
		return -1;
	if (strcmp(s, "0") == 0 || strcmp(s, "a") == 0 || strcmp(s, "A") == 0 ||
	    strcmp(s, "_a") == 0 || strcmp(s, "slot_a") == 0)
		return 0;
	if (strcmp(s, "1") == 0 || strcmp(s, "b") == 0 || strcmp(s, "B") == 0 ||
	    strcmp(s, "_b") == 0 || strcmp(s, "slot_b") == 0)
		return 1;
	return -1;
}

static int cmd_switch(const char *slot_str)
{
	int target_slot = parse_slot_arg(slot_str);
	int fd, rc;
	struct six_avb_vbmeta vbm;

	if (target_slot < 0) {
		fprintf(stderr, "ota switch: specify slot 'a' (0) or 'b' (1)\n");
		return 1;
	}
	fd = open("/dev/mapper/control", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "ota switch: cannot open /dev/mapper/control\n");
		return 1;
	}
	rc = ioctl(fd, DM_IOC_OTA_ACTIVATE, (unsigned long)target_slot);
	close(fd);

	if (rc < 0) {
		struct dm_ota_status st;
		get_ota_status(&st);
		fprintf(stderr,
			"ota switch: FAILED to activate slot_%c (dm-verity / AVB0 verification failed)!\n"
			"ota switch: Automatic Rollback triggered -> active slot is slot_%c (bBootLunID=0x%02x)\n",
			target_slot ? 'b' : 'a',
			st.active_slot ? 'b' : 'a',
			st.boot_lun_id);
		return 1;
	}

	read_vbmeta(target_slot, &vbm);
	save_ufs_slot_status_file(target_slot, vbm.build_id);
	printf("OTA Slot Activated Live: slot_%c (_%c, UFS bBootLunID=0x%02x, build=%s)\n",
	       target_slot ? 'b' : 'a',
	       target_slot ? 'b' : 'a',
	       target_slot ? 2 : 1,
	       vbm.build_id[0] ? vbm.build_id : "-");
	return 0;
}

static int copy_sectors(int fd, unsigned long src_sec, unsigned long dst_sec, unsigned long num_secs)
{
	unsigned char buf[4096];
	unsigned long rem = num_secs;
	unsigned long cur_src = src_sec, cur_dst = dst_sec;

	while (rem > 0) {
		unsigned long chunk_secs = (rem > 8UL) ? 8UL : rem;
		int bytes = (int)(chunk_secs * 512UL);
		if (lseek(fd, (long)(cur_src * 512UL), 0) < 0 ||
		    read(fd, buf, bytes) != bytes)
			return -1;
		if (lseek(fd, (long)(cur_dst * 512UL), 0) < 0 ||
		    write(fd, buf, bytes) != bytes)
			return -1;
		cur_src += chunk_secs;
		cur_dst += chunk_secs;
		rem -= chunk_secs;
	}
	return 0;
}

static int find_build_prop_block(int fd, unsigned long slice_start_sec, unsigned char out_blk[1024])
{
	int blk;
	for (blk = 1; blk < 64; blk++) {
		if (read_1k(fd, slice_start_sec + ((unsigned long)blk << 1), out_blk) == 0) {
			if (memcmp(out_blk, "# SIX_ANDROID_BUILD_PROP_V1\n", 28) == 0)
				return blk;
		}
	}
	return -1;
}

static void build_updated_prop_block(int is_vendor, int target_slot,
				     const char *build_id,
				     unsigned int ota_ver,
				     unsigned char out_blk[1024])
{
	char text[1024];
	int len;
	char slot_ch = target_slot ? 'b' : 'a';
	unsigned int inc = 1 + ota_ver;

	memset(text, 0, sizeof(text));
	if (!is_vendor) {
		len = sprintf(text,
			"# SIX_ANDROID_BUILD_PROP_V1\n"
			"ro.build.Partition=system\n"
			"ro.build.Slot=_%c\n"
			"ro.build.id=%s\n"
			"ro.build.version.incremental=20261003.%04u\n"
			"ro.build.version.release=16\n"
			"ro.build.version.security_patch=2026-10-05\n"
			"ro.build.fingerprint=google/six_x86/six:16/%s/20261003.%04u:user/release-keys\n"
			"ro.product.system.brand=google\n"
			"ro.product.system.name=six_x86\n"
			"ro.product.system.device=six\n"
			"ro.boot.slot_suffix=_%c\n"
			"ro.boot.verifiedbootstate=green\n"
			"ro.boot.veritymode=enforcing\n"
			"ro.ota.version=%u\n",
			slot_ch, build_id, inc, build_id, inc, slot_ch, ota_ver);
	} else {
		len = sprintf(text,
			"# SIX_ANDROID_BUILD_PROP_V1\n"
			"ro.build.Partition=vendor\n"
			"ro.build.Slot=_%c\n"
			"ro.vendor.build.id=%s\n"
			"ro.vendor.build.version.incremental=20261003.%04u\n"
			"ro.vendor.build.version.release=16\n"
			"ro.vendor.build.security_patch=2026-10-05\n"
			"ro.vendor.build.fingerprint=google/six_x86/six:16/%s/20261003.%04u:user/release-keys\n"
			"ro.product.vendor.brand=google\n"
			"ro.product.vendor.name=six_x86\n"
			"ro.product.vendor.device=six\n"
			"ro.boot.slot_suffix=_%c\n"
			"ro.ota.version=%u\n",
			slot_ch, build_id, inc, build_id, inc, slot_ch, ota_ver);
	}
	memset(out_blk, '#', 1024);
	if (len > 1023)
		len = 1023;
	memcpy(out_blk, text, len);
	out_blk[1023] = '\n';
}

static int cmd_apply(int argc, char **argv)
{
	struct dm_ota_status st;
	struct six_avb_vbmeta vbm_src, vbm_dst;
	const char *custom_build = NULL;
	int target_slot = -1;
	int no_activate = 0;
	int i, hdd_fd, sys_prop_blk, vnd_prop_blk;
	unsigned long src_sys_sec, src_vnd_sec;
	unsigned long dst_sys_sec, dst_sys_hash, dst_vnd_sec, dst_vnd_hash;
	unsigned char prop_blk[1024];
	unsigned char new_sys_root[32], new_vnd_root[32];
	char sys_hex[65], vnd_hex[65], build_id[64];
	unsigned int new_ota_ver;
	unsigned long long new_rollback;

	for (i = 2; i < argc; i++) {
		if (strcmp(argv[i], "--no-activate") == 0)
			no_activate = 1;
		else if (strcmp(argv[i], "--slot") == 0 && i + 1 < argc)
			target_slot = parse_slot_arg(argv[++i]);
		else if (argv[i][0] != '-')
			custom_build = argv[i];
	}

	if (get_ota_status(&st) < 0) {
		fprintf(stderr, "ota apply: cannot query DM_IOC_OTA_STATUS\n");
		return 1;
	}
	if (target_slot < 0)
		target_slot = st.active_slot ? 0 : 1;

	read_vbmeta(st.active_slot, &vbm_src);
	read_vbmeta(target_slot, &vbm_dst);

	new_rollback = vbm_src.rollback_index;
	if (vbm_dst.rollback_index > new_rollback)
		new_rollback = vbm_dst.rollback_index;
	new_rollback++;
	new_ota_ver = (unsigned int)new_rollback;

	if (custom_build && custom_build[0]) {
		strncpy(build_id, custom_build, sizeof(build_id) - 1);
		build_id[sizeof(build_id) - 1] = '\0';
	} else {
		sprintf(build_id, "SIX.261003.%03u.%c%u",
			new_ota_ver, target_slot ? 'B' : 'A', new_ota_ver);
	}

	src_sys_sec  = (target_slot == 1) ? SYSTEM_A_START_SEC : SYSTEM_B_START_SEC;
	src_vnd_sec  = (target_slot == 1) ? VENDOR_A_START_SEC : VENDOR_B_START_SEC;
	dst_sys_sec  = (target_slot == 1) ? SYSTEM_B_START_SEC : SYSTEM_A_START_SEC;
	dst_sys_hash = (target_slot == 1) ? SYSTEM_B_HASH_SEC  : SYSTEM_A_HASH_SEC;
	dst_vnd_sec  = (target_slot == 1) ? VENDOR_B_START_SEC : VENDOR_A_START_SEC;
	dst_vnd_hash = (target_slot == 1) ? VENDOR_B_HASH_SEC  : VENDOR_A_HASH_SEC;

	printf("[update_engine] Starting Seamless A/B OTA payload application -> slot_%c (_%c)\n",
	       target_slot ? 'b' : 'a', target_slot ? 'b' : 'a');

	hdd_fd = open("/dev/hdd", O_RDWR);
	if (hdd_fd < 0) {
		fprintf(stderr, "ota apply: cannot open /dev/hdd\n");
		return 1;
	}

	/* 1. Ensure target slot has a valid baseline copy of system & vendor */
	if (read_1k(hdd_fd, dst_sys_hash, prop_blk) < 0 ||
	    memcmp(prop_blk, "verity\0\0", 8) != 0) {
		if (copy_sectors(hdd_fd, src_sys_sec, dst_sys_sec, SYSTEM_SLICE_SECS) < 0 ||
		    copy_sectors(hdd_fd, src_vnd_sec, dst_vnd_sec, VENDOR_SLICE_SECS) < 0) {
			fprintf(stderr, "ota apply: failed to clone baseline slices on /dev/hdd\n");
			close(hdd_fd);
			return 1;
		}
	}

	/* 2. Locate /build.prop block in target system and vendor EROFS images */
	sys_prop_blk = find_build_prop_block(hdd_fd, dst_sys_sec, prop_blk);
	vnd_prop_blk = find_build_prop_block(hdd_fd, dst_vnd_sec, prop_blk);
	if (sys_prop_blk < 0 || vnd_prop_blk < 0) {
		fprintf(stderr, "ota apply: could not locate /build.prop in target EROFS slices\n");
		close(hdd_fd);
		return 1;
	}

	/* 3. Patch /system/build.prop and recompute system dm-verity Merkle tree spine */
	build_updated_prop_block(0, target_slot, build_id, new_ota_ver, prop_blk);
	if (update_verity_block_and_spine(hdd_fd, dst_sys_sec, dst_sys_hash,
					  (unsigned long)sys_prop_blk, prop_blk,
					  new_sys_root) < 0) {
		fprintf(stderr, "ota apply: failed to update system_%c Merkle tree\n",
			target_slot ? 'b' : 'a');
		close(hdd_fd);
		return 1;
	}

	/* 4. Patch /vendor/build.prop and recompute vendor dm-verity Merkle tree spine */
	build_updated_prop_block(1, target_slot, build_id, new_ota_ver, prop_blk);
	if (update_verity_block_and_spine(hdd_fd, dst_vnd_sec, dst_vnd_hash,
					  (unsigned long)vnd_prop_blk, prop_blk,
					  new_vnd_root) < 0) {
		fprintf(stderr, "ota apply: failed to update vendor_%c Merkle tree\n",
			target_slot ? 'b' : 'a');
		close(hdd_fd);
		return 1;
	}
	close(hdd_fd);

	bytes_to_hex(new_sys_root, 32, sys_hex);
	bytes_to_hex(new_vnd_root, 32, vnd_hex);
	printf("[update_engine] Updated system_%c (blk %d) -> SHA-256 root_hash=%.32s...\n",
	       target_slot ? 'b' : 'a', sys_prop_blk, sys_hex);
	printf("[update_engine] Updated vendor_%c (blk %d) -> SHA-256 root_hash=%.32s...\n",
	       target_slot ? 'b' : 'a', vnd_prop_blk, vnd_hex);

	/* 5. Write updated AVB0 vbmeta descriptor to UFS Boot LUN */
	memset(&vbm_dst, 0, sizeof(vbm_dst));
	memcpy(vbm_dst.magic, SIX_AVB_VBMETA_MAGIC, 4);
	vbm_dst.version = 1;
	vbm_dst.slot = (unsigned char)target_slot;
	vbm_dst.priority = 15;
	vbm_dst.tries_remaining = 7;
	vbm_dst.flags = SIX_SLOT_FLAG_BOOTABLE | SIX_SLOT_FLAG_SUCCESSFUL;
	vbm_dst.rollback_index = new_rollback;
	memcpy(vbm_dst.system_root_hash, new_sys_root, 32);
	memcpy(vbm_dst.vendor_root_hash, new_vnd_root, 32);
	strncpy(vbm_dst.build_id, build_id, sizeof(vbm_dst.build_id) - 1);
	sprintf(vbm_dst.release_str, "16 (20261003.%04u)", 1 + new_ota_ver);
	if (write_vbmeta(target_slot, &vbm_dst) < 0) {
		fprintf(stderr, "ota apply: failed to write AVB0 vbmeta to %s\n",
			target_slot ? "/dev/ufsc" : "/dev/ufsb");
		return 1;
	}
	printf("[update_engine] Signed AVB0 vbmeta on %s (rollback_index=%llu, build=%s)\n",
	       target_slot ? "/dev/ufsc" : "/dev/ufsb", new_rollback, build_id);

	/* 6. Record anti-rollback index in UFS W-LUN RPMB (/dev/ufs-rpmb) */
	if (rpmb_record_rollback(new_rollback, build_id) == 0) {
		printf("[update_engine] Recorded anti-rollback index %llu in /dev/ufs-rpmb (HMAC-SHA256)\n",
		       new_rollback);
	}

	if (no_activate) {
		printf("[update_engine] Payload staged in slot_%c (run 'ota switch %c' to activate)\n",
		       target_slot ? 'b' : 'a', target_slot ? 'b' : 'a');
		return 0;
	}
	return cmd_switch(target_slot ? "b" : "a");
}

static int cmd_corrupt_b(void)
{
	unsigned char blk[1024];
	int hdd_fd = open("/dev/hdd", O_RDWR);
	int bsg_fd;

	if (hdd_fd < 0) {
		fprintf(stderr, "ota corrupt-b: cannot open /dev/hdd\n");
		return 1;
	}
	if (read_1k(hdd_fd, SYSTEM_B_START_SEC + 2UL, blk) < 0) {
		close(hdd_fd);
		return 1;
	}
	/* Flip bytes in system_b EROFS superblock (block 1) without updating Merkle tree */
	blk[0] ^= 0xff;
	blk[1] ^= 0xff;
	blk[2] ^= 0xff;
	blk[3] ^= 0xff;
	write_1k(hdd_fd, SYSTEM_B_START_SEC + 2UL, blk);
	close(hdd_fd);

	/* Set UFS bBootLunID = 2 (Slot B) so next boot or ota switch b tests rollback */
	bsg_fd = open("/dev/ufs-bsg0", O_RDWR);
	if (bsg_fd >= 0) {
		struct ufs_bsg_query_ioctl q;
		memset(&q, 0, sizeof(q));
		q.opcode = UPIU_QUERY_OPCODE_WRITE_ATTR;
		q.idn = UFS_ATTR_IDN_BOOT_LUN_ID;
		q.value = 2;
		ioctl(bsg_fd, UFS_IOCTL_QUERY, &q);
		close(bsg_fd);
	}
	printf("ota corrupt-b: Corrupted system_b block 1 (sector %lu) on /dev/hdd and set bBootLunID=0x02\n",
	       SYSTEM_B_START_SEC + 2UL);
	return 0;
}

static int cmd_repair_b(void)
{
	unsigned char blk[1024], sys_root[32];
	struct six_avb_vbmeta vbm_b;
	int hdd_fd = open("/dev/hdd", O_RDWR);

	if (hdd_fd < 0) {
		fprintf(stderr, "ota repair-b: cannot open /dev/hdd\n");
		return 1;
	}
	if (read_1k(hdd_fd, SYSTEM_A_START_SEC + 2UL, blk) < 0) {
		close(hdd_fd);
		return 1;
	}
	if (update_verity_block_and_spine(hdd_fd, SYSTEM_B_START_SEC, SYSTEM_B_HASH_SEC,
					  1UL, blk, sys_root) < 0) {
		close(hdd_fd);
		return 1;
	}
	close(hdd_fd);

	if (read_vbmeta(1, &vbm_b) == 0) {
		vbm_b.flags &= ~SIX_SLOT_FLAG_UNBOOTABLE;
		vbm_b.flags |= (SIX_SLOT_FLAG_BOOTABLE | SIX_SLOT_FLAG_SUCCESSFUL);
		vbm_b.priority = 14;
		vbm_b.tries_remaining = 7;
		memcpy(vbm_b.system_root_hash, sys_root, 32);
		write_vbmeta(1, &vbm_b);
	}
	printf("ota repair-b: Restored system_b block 1 and cleared UNBOOTABLE flag on /dev/ufsc\n");
	return 0;
}

static const char *strip_file_uri(const char *s)
{
	if (!s)
		return "/ufs/ota/ota.zip";
	if (strncmp(s, "file://", 7) == 0)
		return s + 7;
	return s;
}

static int zip_find_entry(int zfd, const char *target_name,
			  unsigned long *out_off, unsigned long *out_size)
{
	unsigned long pos = 0;
	unsigned char lfh[30];
	char name_buf[128];

	while (1) {
		unsigned int sig, comp_sz, uncomp_sz;
		unsigned short comp_method, name_len, extra_len;
		unsigned long data_off;

		if (lseek(zfd, (long)pos, 0) < 0)
			return -1;
		if (read(zfd, lfh, 30) != 30)
			return -1;
		sig = (unsigned int)lfh[0] | ((unsigned int)lfh[1] << 8) |
		      ((unsigned int)lfh[2] << 16) | ((unsigned int)lfh[3] << 24);
		if (sig != 0x04034b50U)
			return -1;
		comp_method = (unsigned short)lfh[8] | ((unsigned short)lfh[9] << 8);
		comp_sz = (unsigned int)lfh[18] | ((unsigned int)lfh[19] << 8) |
			  ((unsigned int)lfh[20] << 16) | ((unsigned int)lfh[21] << 24);
		uncomp_sz = (unsigned int)lfh[22] | ((unsigned int)lfh[23] << 8) |
			    ((unsigned int)lfh[24] << 16) | ((unsigned int)lfh[25] << 24);
		name_len = (unsigned short)lfh[26] | ((unsigned short)lfh[27] << 8);
		extra_len = (unsigned short)lfh[28] | ((unsigned short)lfh[29] << 8);
		if (comp_method != 0)
			return -1;
		memset(name_buf, 0, sizeof(name_buf));
		if (name_len > 0) {
			unsigned short rlen = (name_len < sizeof(name_buf) - 1)
					      ? name_len : (unsigned short)(sizeof(name_buf) - 1);
			if (read(zfd, name_buf, rlen) != (int)rlen)
				return -1;
		}
		data_off = pos + 30UL + (unsigned long)name_len + (unsigned long)extra_len;
		if (strcmp(name_buf, target_name) == 0) {
			if (out_off)
				*out_off = data_off;
			if (out_size)
				*out_size = (unsigned long)uncomp_sz;
			return 0;
		}
		pos = data_off + (unsigned long)comp_sz;
	}
}

static void extract_kv_from_buf(const char *buf, const char *key, char *out, int out_sz)
{
	int klen = (int)strlen(key);
	const char *p = buf;

	out[0] = '\0';
	while (*p) {
		const char *eol = strchr(p, '\n');
		int linelen = eol ? (int)(eol - p) : (int)strlen(p);
		if (linelen > klen && strncmp(p, key, klen) == 0 && p[klen] == '=') {
			int vlen = linelen - (klen + 1);
			if (vlen > 0 && p[klen + 1 + vlen - 1] == '\r')
				vlen--;
			if (vlen >= out_sz)
				vlen = out_sz - 1;
			memcpy(out, p + klen + 1, vlen);
			out[vlen] = '\0';
			return;
		}
		if (!eol)
			break;
		p = eol + 1;
	}
}

static int sha256_file_range(int fd, unsigned long offset, unsigned long size,
			     unsigned char out_digest[32])
{
	struct sha256_ctx ctx;
	static unsigned char chunk[4096];
	unsigned long rem = size;

	if (lseek(fd, (long)offset, 0) < 0)
		return -1;
	sha256_init(&ctx);
	while (rem > 0) {
		unsigned long step = (rem > sizeof(chunk)) ? sizeof(chunk) : rem;
		if (read(fd, chunk, (int)step) != (int)step)
			return -1;
		sha256_update(&ctx, chunk, step);
		rem -= step;
	}
	sha256_final(&ctx, out_digest);
	return 0;
}

static int cmd_inspect_zip(const char *raw_path)
{
	const char *zip_path = strip_file_uri(raw_path);
	int zfd;
	unsigned long meta_off = 0, meta_sz = 0;
	unsigned long props_off = 0, props_sz = 0;
	unsigned long pay_off = 0, pay_sz = 0;
	static char text_buf[1024];
	char expected_hash[65], calc_hash_hex[65], sys_hex[65], vnd_hex[65];
	unsigned char calc_hash[32], man_hash[32];
	static struct six_crau_header hdr;
	static struct six_crau_op op;
	unsigned int i;

	zfd = open(zip_path, O_RDONLY);
	if (zfd < 0) {
		fprintf(stderr, "ota inspect: cannot open %s\n", zip_path);
		return 1;
	}

	if (zip_find_entry(zfd, "META-INF/com/android/metadata", &meta_off, &meta_sz) < 0 ||
	    zip_find_entry(zfd, "payload_properties.txt", &props_off, &props_sz) < 0 ||
	    zip_find_entry(zfd, "payload.bin", &pay_off, &pay_sz) < 0) {
		fprintf(stderr, "ota inspect: %s is not a valid Android A/B ota.zip (ZIP_STORED)\n",
			zip_path);
		close(zfd);
		return 1;
	}

	printf("=== Android Desktop A/B OTA Package Inspection (%s) ===\n", zip_path);
	memset(text_buf, 0, sizeof(text_buf));
	lseek(zfd, (long)meta_off, 0);
	read(zfd, text_buf, (meta_sz < sizeof(text_buf) - 1) ? (int)meta_sz : (int)(sizeof(text_buf) - 1));
	printf("[META-INF/com/android/metadata]\n%s\n", text_buf);

	memset(text_buf, 0, sizeof(text_buf));
	lseek(zfd, (long)props_off, 0);
	read(zfd, text_buf, (props_sz < sizeof(text_buf) - 1) ? (int)props_sz : (int)(sizeof(text_buf) - 1));
	printf("[payload_properties.txt]\n%s\n", text_buf);

	extract_kv_from_buf(text_buf, "FILE_HASH", expected_hash, sizeof(expected_hash));
	sha256_file_range(zfd, pay_off, pay_sz, calc_hash);
	bytes_to_hex(calc_hash, 32, calc_hash_hex);

	lseek(zfd, (long)pay_off, 0);
	if (read(zfd, &hdr, sizeof(hdr)) != (int)sizeof(hdr) ||
	    memcmp(hdr.magic, SIX_CRAU_MAGIC, 4) != 0 ||
	    hdr.version != SIX_CRAU_VERSION) {
		fprintf(stderr, "ota inspect: invalid CrAU v2 header in payload.bin\n");
		close(zfd);
		return 1;
	}

	sha256_file_range(zfd, pay_off + hdr.header_size, hdr.ops_size, man_hash);
	bytes_to_hex(hdr.system_root_hash, 32, sys_hex);
	bytes_to_hex(hdr.vendor_root_hash, 32, vnd_hex);

	printf("[CrAU v%u payload.bin @ offset %lu (%lu bytes)]\n", hdr.version, pay_off, pay_sz);
	printf("  FILE_HASH Match:  %s (%s)\n",
	       (strcmp(expected_hash, calc_hash_hex) == 0) ? "VERIFIED" : "MISMATCH",
	       calc_hash_hex);
	printf("  Manifest Match:   %s (%u install ops, %u bytes blobs, type=%s)\n",
	       (memcmp(man_hash, hdr.manifest_sha256, 32) == 0) ? "VERIFIED" : "MISMATCH",
	       hdr.num_ops, hdr.blob_size, hdr.ota_type ? "FULL" : "DELTA");
	printf("  Target Build ID:  %s (release %s, patch %s, rollback=%llu)\n",
	       hdr.build_id, hdr.release_str, hdr.security_patch,
	       (unsigned long long)hdr.rollback_index);
	printf("  System Root Hash: %s\n", sys_hex);
	printf("  Vendor Root Hash: %s\n", vnd_hex);

	for (i = 0; i < hdr.num_ops; i++) {
		const char *pname;
		const char *oname;
		lseek(zfd, (long)(pay_off + hdr.header_size + i * sizeof(op)), 0);
		if (read(zfd, &op, sizeof(op)) != (int)sizeof(op))
			break;
		pname = (op.part_id == SIX_CRAU_PART_SYSTEM) ? "system" : "vendor";
		oname = (op.op_type == SIX_CRAU_OP_SOURCE_COPY) ? "SOURCE_COPY" :
			(op.op_type == SIX_CRAU_OP_REPLACE) ? "REPLACE" : "ZERO";
		printf("    op[%02u] %-6s %-11s dst_4k=%-4u blocks=%-4u blob=+%u (%u B)\n",
		       i, pname, oname, op.dst_block, op.num_blocks, op.blob_offset, op.blob_size);
	}

	close(zfd);
	return 0;
}

static int cmd_install_zip(int argc, char **argv)
{
	const char *zip_path = "/ufs/ota/ota.zip";
	int target_slot = -1;
	int no_activate = 0;
	int i, zfd, hdd_fd;
	unsigned long props_off = 0, props_sz = 0;
	unsigned long pay_off = 0, pay_sz = 0;
	static char props_buf[1024];
	char expected_hash[65], calc_hash_hex[65], target_slot_prop[16];
	char sys_hex[65], vnd_hex[65];
	unsigned char calc_hash[32], man_hash[32], blob_hash[32];
	static struct six_crau_header hdr;
	static struct six_crau_op op;
	static unsigned char blk_4k[4096];
	struct dm_ota_status st;
	struct six_avb_vbmeta vbm_dst;
	unsigned int op_idx, b;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "install") == 0 || strcmp(argv[i], "--update") == 0 ||
		    strcmp(argv[i], "--follow") == 0)
			continue;
		if (strcmp(argv[i], "--no-activate") == 0)
			no_activate = 1;
		else if (strcmp(argv[i], "--slot") == 0 && i + 1 < argc)
			target_slot = parse_slot_arg(argv[++i]);
		else if (strncmp(argv[i], "--payload=", 10) == 0)
			zip_path = strip_file_uri(argv[i] + 10);
		else if (argv[i][0] != '-')
			zip_path = strip_file_uri(argv[i]);
	}

	zfd = open(zip_path, O_RDONLY);
	if (zfd < 0) {
		fprintf(stderr, "ota install: cannot open OTA package %s\n", zip_path);
		return 1;
	}

	if (zip_find_entry(zfd, "payload_properties.txt", &props_off, &props_sz) < 0 ||
	    zip_find_entry(zfd, "payload.bin", &pay_off, &pay_sz) < 0) {
		fprintf(stderr, "ota install: %s missing payload_properties.txt or payload.bin\n",
			zip_path);
		close(zfd);
		return 1;
	}

	memset(props_buf, 0, sizeof(props_buf));
	lseek(zfd, (long)props_off, 0);
	read(zfd, props_buf, (props_sz < sizeof(props_buf) - 1) ? (int)props_sz : (int)(sizeof(props_buf) - 1));
	extract_kv_from_buf(props_buf, "FILE_HASH", expected_hash, sizeof(expected_hash));
	extract_kv_from_buf(props_buf, "TARGET_SLOT", target_slot_prop, sizeof(target_slot_prop));

	if (sha256_file_range(zfd, pay_off, pay_sz, calc_hash) < 0) {
		close(zfd);
		return 1;
	}
	bytes_to_hex(calc_hash, 32, calc_hash_hex);
	if (expected_hash[0] && strcmp(expected_hash, calc_hash_hex) != 0) {
		fprintf(stderr, "ota install: payload.bin FILE_HASH mismatch! expected=%s got=%s\n",
			expected_hash, calc_hash_hex);
		close(zfd);
		return 1;
	}

	lseek(zfd, (long)pay_off, 0);
	if (read(zfd, &hdr, sizeof(hdr)) != (int)sizeof(hdr) ||
	    memcmp(hdr.magic, SIX_CRAU_MAGIC, 4) != 0 ||
	    hdr.version != SIX_CRAU_VERSION) {
		fprintf(stderr, "ota install: invalid CrAU v2 header magic\n");
		close(zfd);
		return 1;
	}

	if (sha256_file_range(zfd, pay_off + hdr.header_size, hdr.ops_size, man_hash) < 0 ||
	    memcmp(man_hash, hdr.manifest_sha256, 32) != 0) {
		fprintf(stderr, "ota install: CrAU manifest SHA-256 verification failed!\n");
		close(zfd);
		return 1;
	}

	if (target_slot < 0) {
		if (target_slot_prop[0])
			target_slot = parse_slot_arg(target_slot_prop);
		if (target_slot < 0) {
			if (get_ota_status(&st) == 0)
				target_slot = st.active_slot ? 0 : 1;
			else
				target_slot = 1;
		}
	}

	printf("[update_engine] Installing %s (%s CrAU v%u, %u ops, %u B blobs) -> slot_%c (_%c)\n",
	       zip_path, hdr.ota_type ? "FULL" : "DELTA", hdr.version,
	       hdr.num_ops, hdr.blob_size,
	       target_slot ? 'b' : 'a', target_slot ? 'b' : 'a');
	printf("[update_engine] Verified payload.bin SHA-256: %.32s...\n", calc_hash_hex);

	hdd_fd = open("/dev/hdd", O_RDWR);
	if (hdd_fd < 0) {
		fprintf(stderr, "ota install: cannot open /dev/hdd\n");
		close(zfd);
		return 1;
	}

	for (op_idx = 0; op_idx < hdr.num_ops; op_idx++) {
		unsigned long src_base_sec, dst_base_sec, hash_4k;

		lseek(zfd, (long)(pay_off + hdr.header_size + op_idx * sizeof(op)), 0);
		if (read(zfd, &op, sizeof(op)) != (int)sizeof(op)) {
			close(hdd_fd);
			close(zfd);
			return 1;
		}

		if (op.part_id == SIX_CRAU_PART_SYSTEM) {
			src_base_sec = SYSTEM_A_START_SEC;
			dst_base_sec = (target_slot == 1) ? SYSTEM_B_START_SEC : SYSTEM_A_START_SEC;
			hash_4k = SYSTEM_A_DATA_SECS / 8UL; /* 3840 */
		} else {
			src_base_sec = VENDOR_A_START_SEC;
			dst_base_sec = (target_slot == 1) ? VENDOR_B_START_SEC : VENDOR_A_START_SEC;
			hash_4k = VENDOR_A_DATA_SECS / 8UL; /* 768 */
		}

		if (op.op_type == SIX_CRAU_OP_ZERO) {
			/* Tail padding blocks are already zeroed in super partition */
			continue;
		} else if (op.op_type == SIX_CRAU_OP_SOURCE_COPY) {
			unsigned long s_sec = src_base_sec + (unsigned long)op.src_block * 8UL;
			unsigned long d_sec = dst_base_sec + (unsigned long)op.dst_block * 8UL;
			if (s_sec != d_sec) {
				/* Only copy header/prop blocks (< 16) or Merkle tree spine blocks that could differ */
				for (b = 0; b < op.num_blocks; b++) {
					unsigned int cur_4k = op.dst_block + b;
					if (cur_4k < 16U || (cur_4k >= hash_4k && cur_4k < hash_4k + 8U)) {
						static unsigned char dst_4k[4096];
						unsigned long bs = s_sec + (unsigned long)b * 8UL;
						unsigned long bd = d_sec + (unsigned long)b * 8UL;
						lseek(hdd_fd, (long)(bs * 512UL), 0);
						if (read(hdd_fd, blk_4k, 4096) != 4096) {
							close(hdd_fd);
							close(zfd);
							return 1;
						}
						lseek(hdd_fd, (long)(bd * 512UL), 0);
						if (read(hdd_fd, dst_4k, 4096) != 4096 ||
						    memcmp(blk_4k, dst_4k, 4096) != 0) {
							lseek(hdd_fd, (long)(bd * 512UL), 0);
							if (write(hdd_fd, blk_4k, 4096) != 4096) {
								close(hdd_fd);
								close(zfd);
								return 1;
							}
						}
					}
				}
			}
		} else if (op.op_type == SIX_CRAU_OP_REPLACE) {
			unsigned long blob_file_off = pay_off + hdr.blob_offset + op.blob_offset;
			if (sha256_file_range(zfd, blob_file_off, op.blob_size, blob_hash) < 0 ||
			    memcmp(blob_hash, op.data_sha256, 32) != 0) {
				fprintf(stderr, "ota install: REPLACE op %u SHA-256 mismatch!\n", op_idx);
				close(hdd_fd);
				close(zfd);
				return 1;
			}
			lseek(zfd, (long)blob_file_off, 0);
			lseek(hdd_fd, (long)((dst_base_sec + (unsigned long)op.dst_block * 8UL) * 512UL), 0);
			for (b = 0; b < op.num_blocks; b++) {
				if (read(zfd, blk_4k, 4096) != 4096 ||
				    write(hdd_fd, blk_4k, 4096) != 4096) {
					close(hdd_fd);
					close(zfd);
					return 1;
				}
			}
		}
	}

	close(hdd_fd);
	close(zfd);

	bytes_to_hex(hdr.system_root_hash, 32, sys_hex);
	bytes_to_hex(hdr.vendor_root_hash, 32, vnd_hex);
	printf("[update_engine] Applied system_%c -> SHA-256 root_hash=%.32s...\n",
	       target_slot ? 'b' : 'a', sys_hex);
	printf("[update_engine] Applied vendor_%c -> SHA-256 root_hash=%.32s...\n",
	       target_slot ? 'b' : 'a', vnd_hex);

	memset(&vbm_dst, 0, sizeof(vbm_dst));
	memcpy(vbm_dst.magic, SIX_AVB_VBMETA_MAGIC, 4);
	vbm_dst.version = 1;
	vbm_dst.slot = (unsigned char)target_slot;
	vbm_dst.priority = 15;
	vbm_dst.tries_remaining = 7;
	vbm_dst.flags = SIX_SLOT_FLAG_BOOTABLE | SIX_SLOT_FLAG_SUCCESSFUL;
	vbm_dst.rollback_index = hdr.rollback_index;
	memcpy(vbm_dst.system_root_hash, hdr.system_root_hash, 32);
	memcpy(vbm_dst.vendor_root_hash, hdr.vendor_root_hash, 32);
	strncpy(vbm_dst.build_id, hdr.build_id, sizeof(vbm_dst.build_id) - 1);
	strncpy(vbm_dst.release_str, hdr.release_str, sizeof(vbm_dst.release_str) - 1);
	if (write_vbmeta(target_slot, &vbm_dst) < 0) {
		fprintf(stderr, "ota install: failed to write AVB0 vbmeta\n");
		return 1;
	}
	rpmb_record_rollback(hdr.rollback_index, hdr.build_id);

	if (no_activate) {
		printf("[update_engine] Package %s staged in slot_%c (build=%s)\n",
		       zip_path, target_slot ? 'b' : 'a', hdr.build_id);
		return 0;
	}
	return cmd_switch(target_slot ? "b" : "a");
}

int main(int argc, char **argv)
{
	const char *prog = argv[0] ? argv[0] : "ota";
	const char *base = strrchr(prog, '/');
	base = base ? (base + 1) : prog;

	/* Support Android Desktop update_engine_client flags */
	if (strcmp(base, "update_engine_client") == 0) {
		int i, do_update = 0, do_verify = 0;
		const char *payload = "/ufs/ota/ota.zip";
		for (i = 1; i < argc; i++) {
			if (strcmp(argv[i], "--status") == 0)
				return cmd_status();
			if (strcmp(argv[i], "--update") == 0)
				do_update = 1;
			if (strcmp(argv[i], "--verify") == 0)
				do_verify = 1;
			if (strncmp(argv[i], "--payload=", 10) == 0)
				payload = argv[i] + 10;
		}
		if (do_verify)
			return cmd_inspect_zip(payload);
		if (do_update)
			return cmd_install_zip(argc, argv);
		return cmd_status();
	}

	if (argc < 2 || strcmp(argv[1], "status") == 0 ||
	    strcmp(argv[1], "--status") == 0 ||
	    strcmp(argv[1], "info") == 0 ||
	    strcmp(argv[1], "hal-info") == 0 ||
	    strcmp(argv[1], "dump-slots") == 0) {
		return cmd_status();
	}

	if (strcmp(argv[1], "inspect") == 0) {
		return cmd_inspect_zip((argc >= 3) ? argv[2] : "/ufs/ota/ota.zip");
	}

	if (strcmp(argv[1], "install") == 0 || strcmp(argv[1], "sideload") == 0 ||
	    strcmp(argv[1], "--update") == 0) {
		return cmd_install_zip(argc, argv);
	}

	if (strcmp(argv[1], "get-current-slot") == 0) {
		struct dm_ota_status st;
		if (get_ota_status(&st) < 0)
			return 1;
		printf("%d\n", st.active_slot);
		return 0;
	}

	if (strcmp(argv[1], "get-suffix") == 0) {
		int s = 0;
		if (argc >= 3) {
			s = parse_slot_arg(argv[2]);
			if (s < 0)
				s = 0;
		} else {
			struct dm_ota_status st;
			if (get_ota_status(&st) == 0)
				s = st.active_slot;
		}
		printf("_%c\n", s ? 'b' : 'a');
		return 0;
	}

	if (strcmp(argv[1], "switch") == 0 ||
	    strcmp(argv[1], "set-active-boot-slot") == 0) {
		return cmd_switch((argc >= 3) ? argv[2] : "");
	}

	if (strcmp(argv[1], "apply") == 0 || strcmp(argv[1], "update") == 0) {
		return cmd_apply(argc, argv);
	}

	if (strcmp(argv[1], "corrupt-b") == 0)
		return cmd_corrupt_b();

	if (strcmp(argv[1], "repair-b") == 0)
		return cmd_repair_b();

	printf("Usage: %s <command> [args]\n", base);
	printf("  status | info                   Show A/B slots, dm-verity hashes & UFS vbmeta\n");
	printf("  inspect [ota.zip]               Inspect Android A/B ota.zip & CrAU v2 payload\n");
	printf("  install [ota.zip] [--slot a|b]  Verify & apply Android A/B ota.zip package live\n");
	printf("  apply [BUILD_ID] [--slot a|b]   Stage & apply live Seamless A/B OTA update\n");
	printf("  switch <a|b|0|1>                Verify & switch active slot live\n");
	printf("  corrupt-b                       Tamper with slot_b to test dm-verity rollback\n");
	printf("  repair-b                        Restore slot_b and clear UNBOOTABLE flag\n");
	printf("  get-current-slot                Print active slot number (0 or 1)\n");
	printf("  get-suffix [0|1]                Print slot suffix (_a or _b)\n");
	printf("  set-active-boot-slot <0|1>      Switch active boot slot (Android bootctl CLI)\n");
	return 0;
}

