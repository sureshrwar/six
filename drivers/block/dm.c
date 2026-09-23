/*
 *  linux/drivers/block/dm.c
 *
 *  Device Mapper (dm) block driver for SIX.
 *  Supports linear, crypt (ChaCha20-256 sector-IV stream cipher),
 *  striped (RAID-0), zero, and error targets on /dev/dm-0..7.
 */

#define MAJOR_NR DM_MAJOR

#include <solaris.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/blk.h>
#include <linux/dm.h>
#include <linux/verity_roothash.h>
#include <asm/segment.h>
#include <asm/system.h>

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

struct dm_device {
	int active;
	int suspended;
	int ro;
	int open_count;
	char name[DM_NAME_LEN];
	unsigned long total_sectors;
	unsigned long read_ios;
	unsigned long write_ios;
	unsigned long read_sectors;
	unsigned long write_sectors;
	int num_targets;
	struct dm_target_spec targets[DM_MAX_TARGETS];
	unsigned int parsed_key[8];
	unsigned char verity_salt[32];
	unsigned char verity_root_hash[32];
};

static struct dm_device dm_devs[DM_MAX_DEVICES];
static int dm_sizes[64];
static int dm_blocksizes[64];

static inline unsigned int rotl32(unsigned int v, int c)
{
	return (v << c) | (v >> (32 - c));
}

static inline unsigned int rotr32(unsigned int v, int c)
{
	return (v >> c) | (v << (32 - c));
}

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

static void sha256_transform(unsigned int state[8], const unsigned char block[64])
{
	unsigned int w[64];
	unsigned int a, b, c, d, e, f, g, h, t1, t2;
	int i;

	for (i = 0; i < 16; i++) {
		w[i] = ((unsigned int)block[i * 4 + 0] << 24) |
		       ((unsigned int)block[i * 4 + 1] << 16) |
		       ((unsigned int)block[i * 4 + 2] << 8)  |
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

/*
 * Compute FIPS 180-4 SHA-256(salt[32] || block[1024]) -> out_digest[32].
 * Total input is 1056 bytes (8448 bits = 0x2100 bits), which spans 17
 * 64-byte SHA-256 blocks including padding.
 */
static void dm_sha256_salted_1k(const unsigned char salt[32],
				const unsigned char blk[1024],
				unsigned char out_digest[32])
{
	unsigned int state[8];
	unsigned char chunk[64];
	int i;

	state[0] = 0x6a09e667U; state[1] = 0xbb67ae85U;
	state[2] = 0x3c6ef372U; state[3] = 0xa54ff53aU;
	state[4] = 0x510e527fU; state[5] = 0x9b05688cU;
	state[6] = 0x1f83d9abU; state[7] = 0x5be0cd19U;

	/* Block 0: salt[0..31] + blk[0..31] */
	memcpy(chunk, salt, 32);
	memcpy(chunk + 32, blk, 32);
	sha256_transform(state, chunk);

	/* Blocks 1..15: blk[32 .. 991] */
	for (i = 0; i < 15; i++)
		sha256_transform(state, blk + 32 + i * 64);

	/* Block 16: blk[992..1023] + 0x80 + 23 zero bytes + 64-bit BE bitlen (8448 = 0x2100) */
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

#define CHACHA_QR(a, b, c, d)			\
	do {					\
		a += b; d ^= a; d = rotl32(d, 16); \
		c += d; b ^= c; b = rotl32(b, 12); \
		a += b; d ^= a; d = rotl32(d, 8);  \
		c += d; b ^= c; b = rotl32(b, 7);  \
	} while (0)

static int hex_nibble(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static void dm_derive_key(const char *key_str, unsigned int out_key[8])
{
	int i, len = strlen(key_str);
	int is_hex = (len >= 16 && (len % 2) == 0);
	unsigned char raw[32];

	for (i = 0; i < len; i++) {
		if (hex_nibble(key_str[i]) < 0) {
			is_hex = 0;
			break;
		}
	}

	memset(raw, 0, sizeof(raw));
	if (is_hex) {
		for (i = 0; i < 32 && (i * 2 + 1) < len; i++) {
			raw[i] = (unsigned char)((hex_nibble(key_str[i * 2]) << 4) |
						 hex_nibble(key_str[i * 2 + 1]));
		}
		for (; i < 32; i++)
			raw[i] = raw[i % (len / 2)] ^ (unsigned char)(i * 0x9e);
	} else {
		unsigned int h = 0x811c9dc5U;
		for (i = 0; i < 32; i++) {
			int j;
			h ^= (unsigned int)(unsigned char)key_str[i % (len ? len : 1)];
			h *= 0x01000193U;
			h ^= (unsigned int)(i * 0x9e3779b9U);
			for (j = 0; j < len; j++) {
				h ^= (unsigned int)(unsigned char)key_str[j];
				h *= 0x01000193U;
			}
			raw[i] = (unsigned char)((h >> ((i & 3) * 8)) & 0xff);
		}
	}

	for (i = 0; i < 8; i++) {
		out_key[i] = ((unsigned int)raw[i * 4 + 0]) |
			     ((unsigned int)raw[i * 4 + 1] << 8) |
			     ((unsigned int)raw[i * 4 + 2] << 16) |
			     ((unsigned int)raw[i * 4 + 3] << 24);
	}
}

/*
 * Encrypt or decrypt one 512-byte sector using 20-round ChaCha20 keyed by
 * the target's 256-bit derived key and the 64-bit sector IV.
 */
static void dm_crypt_sector(const unsigned int key[8], unsigned long iv_sector,
			    const unsigned char *src, unsigned char *dst)
{
	int blk, r, i;

	for (blk = 0; blk < 8; blk++) {
		unsigned int st[16], x[16];
		const unsigned char *s = src + (blk * 64);
		unsigned char *d = dst + (blk * 64);

		st[0]  = 0x61707865U; /* "expa" */
		st[1]  = 0x3320646eU; /* "nd 3" */
		st[2]  = 0x79622d32U; /* "2-by" */
		st[3]  = 0x6b206574U; /* "te k" */
		for (i = 0; i < 8; i++)
			st[4 + i] = key[i];
		st[12] = (unsigned int)blk;
		st[13] = (unsigned int)iv_sector;
		st[14] = 0U;
		st[15] = 0x444d5349U; /* "ISMD" domain tag */

		for (i = 0; i < 16; i++)
			x[i] = st[i];

		for (r = 0; r < 10; r++) {
			CHACHA_QR(x[0], x[4], x[8],  x[12]);
			CHACHA_QR(x[1], x[5], x[9],  x[13]);
			CHACHA_QR(x[2], x[6], x[10], x[14]);
			CHACHA_QR(x[3], x[7], x[11], x[15]);
			CHACHA_QR(x[0], x[5], x[10], x[15]);
			CHACHA_QR(x[1], x[6], x[11], x[12]);
			CHACHA_QR(x[2], x[7], x[8],  x[13]);
			CHACHA_QR(x[3], x[4], x[9],  x[14]);
		}

		for (i = 0; i < 16; i++) {
			unsigned int ks = x[i] + st[i];
			d[i * 4 + 0] = s[i * 4 + 0] ^ (unsigned char)(ks & 0xff);
			d[i * 4 + 1] = s[i * 4 + 1] ^ (unsigned char)((ks >> 8) & 0xff);
			d[i * 4 + 2] = s[i * 4 + 2] ^ (unsigned char)((ks >> 16) & 0xff);
			d[i * 4 + 3] = s[i * 4 + 3] ^ (unsigned char)((ks >> 24) & 0xff);
		}
	}
}

static int dm_rw_phys_sector(kdev_t bdev, unsigned long phys_sec,
			     unsigned char *buf, int cmd)
{
	int major = MAJOR(bdev);
	int minor = MINOR(bdev);

	if (major == HD_MAJOR) {
		int drive = minor >> 6;
		struct buffer_head *bh;
		unsigned long block_nr = phys_sec >> 1;
		int sub_off = (phys_sec & 1) << 9;

		if (drive < 0 || drive >= SIX_MAX_DISKS || six_disk_fd[drive] < 0)
			return -ENODEV;
		if (phys_sec >= (unsigned long)six_disk_sectors[drive])
			return -EIO;

		bh = get_hash_table(bdev, block_nr, 1024);
		if (cmd == READ) {
			if (bh && buffer_uptodate(bh) && buffer_dirty(bh)) {
				memcpy(buf, bh->b_data + sub_off, 512);
				brelse(bh);
				return 0;
			}
			if (bh)
				brelse(bh);
			lseek(six_disk_fd[drive], (long)phys_sec * 512L, 0);
			if (read(six_disk_fd[drive], buf, 512) != 512)
				return -EIO;
			return 0;
		} else {
			lseek(six_disk_fd[drive], (long)phys_sec * 512L, 0);
			if (write(six_disk_fd[drive], buf, 512) != 512) {
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
	return -ENODEV;
}

static unsigned char dm_verity_data_buf[1024];
static unsigned char dm_verity_hash_buf[1024];

static int dm_read_phys_1k(kdev_t bdev, unsigned long phys_sec_even,
			   unsigned char out_1k[1024])
{
	int err;
	err = dm_rw_phys_sector(bdev, phys_sec_even, out_1k, READ);
	if (err)
		return err;
	return dm_rw_phys_sector(bdev, phys_sec_even + 1, out_1k + 512, READ);
}

/*
 * Verify a 1024-byte data block against the 3-level SHA-256 Merkle tree
 * stored starting at t->hash_start_sector on t->bdev, anchored by
 * dev->verity_root_hash.
 */
static int dm_verify_verity_block(struct dm_device *dev,
				  struct dm_target_spec *t,
				  unsigned long blk_nr,
				  const unsigned char data_blk[1024])
{
	kdev_t bdev = to_kdev_t(t->bdev);
	unsigned long hs_sec = t->hash_start_sector;
	unsigned long l2_sec = hs_sec + (1UL << 1);
	unsigned long l1_sec = hs_sec + ((2UL + (blk_nr >> 10)) << 1);
	unsigned long l0_sec = hs_sec + ((17UL + (blk_nr >> 5)) << 1);
	unsigned char d_hash[32], l0_hash[32], l1_hash[32], l2_hash[32];
	unsigned char *hblk = dm_verity_hash_buf;

	/* 1. Hash the 1 KB data block and check Level-0 leaf slot */
	dm_sha256_salted_1k(dev->verity_salt, data_blk, d_hash);
	if (dm_read_phys_1k(bdev, l0_sec, hblk) != 0)
		return -EIO;
	if (memcmp(hblk + ((blk_nr & 31UL) << 5), d_hash, 32) != 0) {
		t->corrupt_blocks++;
		printk("device-mapper: verity: CORRUPTION DETECTED at data block %lu (Level-0 hash mismatch) on %s!\n",
		       blk_nr, dev->name);
		return -EIO;
	}

	/* 2. Hash the Level-0 block and check Level-1 interior slot */
	dm_sha256_salted_1k(dev->verity_salt, hblk, l0_hash);
	if (dm_read_phys_1k(bdev, l1_sec, hblk) != 0)
		return -EIO;
	if (memcmp(hblk + (((blk_nr >> 5) & 31UL) << 5), l0_hash, 32) != 0) {
		t->corrupt_blocks++;
		printk("device-mapper: verity: CORRUPTION DETECTED at data block %lu (Level-1 hash mismatch) on %s!\n",
		       blk_nr, dev->name);
		return -EIO;
	}

	/* 3. Hash the Level-1 block and check Level-2 root block slot */
	dm_sha256_salted_1k(dev->verity_salt, hblk, l1_hash);
	if (dm_read_phys_1k(bdev, l2_sec, hblk) != 0)
		return -EIO;
	if (memcmp(hblk + ((blk_nr >> 10) << 5), l1_hash, 32) != 0) {
		t->corrupt_blocks++;
		printk("device-mapper: verity: CORRUPTION DETECTED at data block %lu (Level-2 hash mismatch) on %s!\n",
		       blk_nr, dev->name);
		return -EIO;
	}

	/* 4. Hash the Level-2 root block and compare against trusted Root Hash */
	dm_sha256_salted_1k(dev->verity_salt, hblk, l2_hash);
	if (memcmp(l2_hash, dev->verity_root_hash, 32) != 0) {
		t->corrupt_blocks++;
		printk("device-mapper: verity: CORRUPTION DETECTED at data block %lu (Root Hash mismatch) on %s!\n",
		       blk_nr, dev->name);
		return -EIO;
	}

	t->verified_blocks++;
	return 0;
}

static int dm_process_sector(struct dm_device *dev, unsigned long sec,
			     unsigned char *buf, int cmd)
{
	int i;
	struct dm_target_spec *t = NULL;
	unsigned long rel_sec, phys_sec;
	unsigned char crypt_buf[512];
	int err;

	for (i = 0; i < dev->num_targets; i++) {
		if (sec >= dev->targets[i].start_sector &&
		    sec < dev->targets[i].start_sector + dev->targets[i].num_sectors) {
			t = &dev->targets[i];
			break;
		}
	}
	if (!t)
		return -EIO;

	rel_sec = sec - t->start_sector;

	switch (t->type) {
	case DM_TARGET_LINEAR:
		phys_sec = t->offset_sector + rel_sec;
		return dm_rw_phys_sector(to_kdev_t(t->bdev), phys_sec, buf, cmd);

	case DM_TARGET_CRYPT:
		phys_sec = t->offset_sector + rel_sec;
		if (cmd == READ) {
			err = dm_rw_phys_sector(to_kdev_t(t->bdev), phys_sec, crypt_buf, READ);
			if (err)
				return err;
			dm_crypt_sector(dev->parsed_key, t->iv_offset + rel_sec, crypt_buf, buf);
			return 0;
		} else {
			dm_crypt_sector(dev->parsed_key, t->iv_offset + rel_sec, buf, crypt_buf);
			return dm_rw_phys_sector(to_kdev_t(t->bdev), phys_sec, crypt_buf, WRITE);
		}

	case DM_TARGET_VERITY: {
		static struct dm_device *last_verity_dev = NULL;
		static unsigned long last_verity_blk = ~0UL;
		unsigned long blk_nr = rel_sec >> 1;
		unsigned long phys_sec_even = t->offset_sector + (blk_nr << 1);
		int sub_off = (rel_sec & 1) << 9;

		if (cmd != READ)
			return -EACCES;
		if (sub_off == 512 && last_verity_dev == dev && last_verity_blk == blk_nr) {
			memcpy(buf, dm_verity_data_buf + 512, 512);
			last_verity_dev = NULL;
			return 0;
		}
		last_verity_dev = NULL;
		err = dm_read_phys_1k(to_kdev_t(t->bdev), phys_sec_even, dm_verity_data_buf);
		if (err)
			return err;
		err = dm_verify_verity_block(dev, t, blk_nr, dm_verity_data_buf);
		if (err)
			return err;
		if (sub_off == 0) {
			last_verity_dev = dev;
			last_verity_blk = blk_nr;
		}
		memcpy(buf, dm_verity_data_buf + sub_off, 512);
		return 0;
	}

	case DM_TARGET_STRIPED: {
		unsigned long chunk = t->chunk_sectors ? t->chunk_sectors : 8;
		unsigned long chunk_idx = rel_sec / chunk;
		unsigned long in_chunk = rel_sec % chunk;
		unsigned long stripe_chunk = chunk_idx / 2;
		int stripe_dev = (int)(chunk_idx % 2);
		kdev_t bdev = to_kdev_t(stripe_dev == 0 ? t->bdev : t->bdev2);
		unsigned long base_off = (stripe_dev == 0) ? t->offset_sector : t->offset_sector2;

		phys_sec = base_off + stripe_chunk * chunk + in_chunk;
		return dm_rw_phys_sector(bdev, phys_sec, buf, cmd);
	}

	case DM_TARGET_ZERO:
		if (cmd == READ)
			memset(buf, 0, 512);
		return 0;

	case DM_TARGET_ERROR:
	default:
		return -EIO;
	}
}

static void do_dm_request(void)
{
	while (CURRENT) {
		int minor = MINOR(CURRENT->rq_dev);
		struct dm_device *dev;
		unsigned long nsect, s;
		int err = 0;

		INIT_REQUEST;

		if (minor < 0 || minor >= DM_MAX_DEVICES) {
			end_request(0);
			continue;
		}
		dev = &dm_devs[minor];
		if (!dev->active || dev->suspended) {
			end_request(0);
			continue;
		}
		if (CURRENT->cmd == WRITE && dev->ro) {
			end_request(0);
			continue;
		}

		nsect = CURRENT->current_nr_sectors;
		if (CURRENT->sector + nsect > dev->total_sectors) {
			end_request(0);
			continue;
		}

		for (s = 0; s < nsect; s++) {
			unsigned char *ptr = (unsigned char *)CURRENT->buffer + (s << 9);
			err = dm_process_sector(dev, CURRENT->sector + s, ptr, CURRENT->cmd);
			if (err)
				break;
		}

		if (err) {
			end_request(0);
			continue;
		}

		if (CURRENT->cmd == READ) {
			dev->read_ios++;
			dev->read_sectors += nsect;
		} else {
			dev->write_ios++;
			dev->write_sectors += nsect;
		}

		CURRENT->sector += nsect;
		CURRENT->buffer += (nsect << 9);
		CURRENT->nr_sectors -= nsect;
		CURRENT->current_nr_sectors = 0;
		end_request(1);
	}
}

static int dm_find_by_name(const char *name)
{
	int i;
	if (!name || !name[0])
		return -1;
	for (i = 0; i < DM_MAX_DEVICES; i++) {
		if (dm_devs[i].active && strcmp(dm_devs[i].name, name) == 0)
			return i;
	}
	return -1;
}

static int dm_resolve_minor(struct dm_ioctl_req *req, int inode_minor)
{
	if (req->name[0]) {
		int m = dm_find_by_name(req->name);
		if (m >= 0)
			return m;
	}
	if (req->minor >= 0 && req->minor < DM_MAX_DEVICES)
		return req->minor;
	if (inode_minor >= 0 && inode_minor < DM_MAX_DEVICES)
		return inode_minor;
	return -1;
}

static void dm_fill_status(int minor, struct dm_ioctl_req *req)
{
	struct dm_device *dev = &dm_devs[minor];
	int i;

	memset(req, 0, sizeof(*req));
	req->minor = minor;
	strncpy(req->name, dev->name, DM_NAME_LEN - 1);
	req->active = dev->active;
	req->suspended = dev->suspended;
	req->ro = dev->ro;
	req->total_sectors = dev->total_sectors;
	req->read_ios = dev->read_ios;
	req->write_ios = dev->write_ios;
	req->read_sectors = dev->read_sectors;
	req->write_sectors = dev->write_sectors;
	req->num_targets = dev->num_targets;
	for (i = 0; i < dev->num_targets && i < DM_MAX_TARGETS; i++)
		req->targets[i] = dev->targets[i];
}

static int dm_ioctl(struct inode *inode, struct file *file,
		    unsigned int cmd, unsigned long arg)
{
	int inode_minor, minor, i, err;
	struct dm_ioctl_req kreq;
	struct dm_device *dev;

	if (!inode)
		return -EINVAL;
	inode_minor = MINOR(inode->i_rdev);

	switch (cmd) {
	case BLKGETSIZE:
		if (inode_minor < 0 || inode_minor >= DM_MAX_DEVICES || !dm_devs[inode_minor].active)
			return -ENODEV;
		if (!arg)
			return -EINVAL;
		err = verify_area(VERIFY_WRITE, (long *)arg, sizeof(long));
		if (err)
			return err;
		put_user(dm_devs[inode_minor].total_sectors, (long *)arg);
		return 0;

	case BLKFLSBUF:
		if (!suser())
			return -EACCES;
		fsync_dev(inode->i_rdev);
		invalidate_buffers(inode->i_rdev);
		return 0;

	RO_IOCTLS(inode->i_rdev, arg);

	case DM_IOC_CREATE:
		if (!arg)
			return -EINVAL;
		err = verify_area(VERIFY_WRITE, (void *)arg, sizeof(struct dm_ioctl_req));
		if (err)
			return err;
		memcpy_fromfs(&kreq, (void *)arg, sizeof(struct dm_ioctl_req));
		kreq.name[DM_NAME_LEN - 1] = '\0';
		if (!kreq.name[0] || kreq.num_targets <= 0 || kreq.num_targets > DM_MAX_TARGETS)
			return -EINVAL;
		if (dm_find_by_name(kreq.name) >= 0)
			return -EEXIST;

		minor = kreq.minor;
		if (minor < 0) {
			for (i = 0; i < DM_MAX_DEVICES; i++) {
				if (!dm_devs[i].active) {
					minor = i;
					break;
				}
			}
		}
		if (minor < 0 || minor >= DM_MAX_DEVICES)
			return -ENOSPC;
		if (dm_devs[minor].active)
			return -EBUSY;

		dev = &dm_devs[minor];
		memset(dev, 0, sizeof(*dev));
		strncpy(dev->name, kreq.name, DM_NAME_LEN - 1);
		dev->ro = kreq.ro;
		dev->num_targets = kreq.num_targets;
		dev->total_sectors = 0;

		for (i = 0; i < kreq.num_targets; i++) {
			unsigned long end_sec;
			dev->targets[i] = kreq.targets[i];
			end_sec = dev->targets[i].start_sector + dev->targets[i].num_sectors;
			if (end_sec > dev->total_sectors)
				dev->total_sectors = end_sec;
			if (dev->targets[i].type == DM_TARGET_CRYPT)
				dm_derive_key(dev->targets[i].key, dev->parsed_key);
			if (dev->targets[i].type == DM_TARGET_VERITY) {
				int j;
				struct six_verity_sb sb;
				dev->ro = 1;
				memset(dev->verity_salt, 0, 32);
				strcpy((char *)dev->verity_salt, VERITY_BIN_SALT_STR);
				for (j = 0; j < 32; j++) {
					int hi = hex_nibble(dev->targets[i].root_hash[j * 2]);
					int lo = hex_nibble(dev->targets[i].root_hash[j * 2 + 1]);
					if (hi >= 0 && lo >= 0)
						dev->verity_root_hash[j] = (unsigned char)((hi << 4) | lo);
				}
				if (dm_read_phys_1k(to_kdev_t(dev->targets[i].bdev),
						    dev->targets[i].hash_start_sector,
						    dm_verity_data_buf) == 0) {
					memcpy(&sb, dm_verity_data_buf, sizeof(sb));
					if (memcmp(sb.magic, "verity\0\0", 8) == 0)
						memcpy(dev->verity_salt, sb.salt, 32);
				}
			}
		}

		dev->active = 1;
		dev->suspended = 0;
		dm_sizes[minor] = dev->total_sectors >> (BLOCK_SIZE_BITS - 9);
		dm_blocksizes[minor] = 1024;
		if (dev->ro)
			set_device_ro(MKDEV(DM_MAJOR, minor), 1);
		else
			set_device_ro(MKDEV(DM_MAJOR, minor), 0);

		dm_fill_status(minor, &kreq);
		memcpy_tofs((void *)arg, &kreq, sizeof(struct dm_ioctl_req));
		printk("device-mapper: created /dev/dm-%d (%s), %lu sectors (%lu KB)\n",
		       minor, dev->name, dev->total_sectors, dev->total_sectors >> 1);
		return 0;

	case DM_IOC_REMOVE: {
		kdev_t kdev;
		if (!arg)
			return -EINVAL;
		err = verify_area(VERIFY_READ, (void *)arg, sizeof(struct dm_ioctl_req));
		if (err)
			return err;
		memcpy_fromfs(&kreq, (void *)arg, sizeof(struct dm_ioctl_req));
		kreq.name[DM_NAME_LEN - 1] = '\0';
		minor = dm_resolve_minor(&kreq, inode_minor);
		if (minor < 0 || !dm_devs[minor].active)
			return -ENODEV;
		dev = &dm_devs[minor];
		if (dev->open_count > 1 || (inode_minor == DM_CONTROL_MINOR && dev->open_count > 0))
			return -EBUSY;

		kdev = MKDEV(DM_MAJOR, minor);
		sync_dev(kdev);
		invalidate_inodes(kdev);
		invalidate_buffers(kdev);
		printk("device-mapper: removed /dev/dm-%d (%s)\n", minor, dev->name);
		memset(dev, 0, sizeof(*dev));
		dm_sizes[minor] = 0;
		return 0;
	}

	case DM_IOC_REMOVE_ALL:
		for (i = 0; i < DM_MAX_DEVICES; i++) {
			if (dm_devs[i].active && dm_devs[i].open_count == 0) {
				kdev_t kdev = MKDEV(DM_MAJOR, i);
				sync_dev(kdev);
				invalidate_inodes(kdev);
				invalidate_buffers(kdev);
				memset(&dm_devs[i], 0, sizeof(struct dm_device));
				dm_sizes[i] = 0;
			}
		}
		return 0;

	case DM_IOC_SUSPEND:
		if (!arg)
			return -EINVAL;
		err = verify_area(VERIFY_READ, (void *)arg, sizeof(struct dm_ioctl_req));
		if (err)
			return err;
		memcpy_fromfs(&kreq, (void *)arg, sizeof(struct dm_ioctl_req));
		kreq.name[DM_NAME_LEN - 1] = '\0';
		minor = dm_resolve_minor(&kreq, inode_minor);
		if (minor < 0 || !dm_devs[minor].active)
			return -ENODEV;
		sync_dev(MKDEV(DM_MAJOR, minor));
		dm_devs[minor].suspended = 1;
		return 0;

	case DM_IOC_RESUME:
		if (!arg)
			return -EINVAL;
		err = verify_area(VERIFY_READ, (void *)arg, sizeof(struct dm_ioctl_req));
		if (err)
			return err;
		memcpy_fromfs(&kreq, (void *)arg, sizeof(struct dm_ioctl_req));
		kreq.name[DM_NAME_LEN - 1] = '\0';
		minor = dm_resolve_minor(&kreq, inode_minor);
		if (minor < 0 || !dm_devs[minor].active)
			return -ENODEV;
		dm_devs[minor].suspended = 0;
		return 0;

	case DM_IOC_STATUS:
		if (!arg)
			return -EINVAL;
		err = verify_area(VERIFY_WRITE, (void *)arg, sizeof(struct dm_ioctl_req));
		if (err)
			return err;
		memcpy_fromfs(&kreq, (void *)arg, sizeof(struct dm_ioctl_req));
		kreq.name[DM_NAME_LEN - 1] = '\0';
		minor = dm_resolve_minor(&kreq, inode_minor);
		if (minor < 0 || minor >= DM_MAX_DEVICES)
			return -ENODEV;
		dm_fill_status(minor, &kreq);
		memcpy_tofs((void *)arg, &kreq, sizeof(struct dm_ioctl_req));
		return 0;

	default:
		return -EINVAL;
	}
}

static int dm_open(struct inode *inode, struct file *filp)
{
	int minor;
	if (!inode)
		return -EINVAL;
	minor = MINOR(inode->i_rdev);
	if (minor == DM_CONTROL_MINOR)
		return 0;
	if (minor < 0 || minor >= DM_MAX_DEVICES)
		return -ENODEV;
	if (!dm_devs[minor].active)
		return -ENODEV;
	dm_devs[minor].open_count++;
	return 0;
}

static void dm_release(struct inode *inode, struct file *filp)
{
	int minor;
	if (!inode)
		return;
	minor = MINOR(inode->i_rdev);
	if (minor == DM_CONTROL_MINOR)
		return;
	sync_dev(inode->i_rdev);
	if (minor >= 0 && minor < DM_MAX_DEVICES && dm_devs[minor].open_count > 0)
		dm_devs[minor].open_count--;
}

static struct file_operations dm_fops = {
	NULL,		/* lseek */
	block_read,	/* read */
	block_write,	/* write */
	NULL,		/* readdir */
	NULL,		/* select */
	dm_ioctl,	/* ioctl */
	NULL,		/* mmap */
	dm_open,	/* open */
	dm_release,	/* release */
	block_fsync	/* fsync */
};

static const char *dm_target_name(int type)
{
	switch (type) {
	case DM_TARGET_LINEAR:  return "linear";
	case DM_TARGET_CRYPT:   return "crypt";
	case DM_TARGET_STRIPED: return "striped";
	case DM_TARGET_ZERO:    return "zero";
	case DM_TARGET_ERROR:   return "error";
	case DM_TARGET_VERITY:  return "verity";
	default:                return "unknown";
	}
}

int get_dm_status_proc(char *buf)
{
	int len = 0, i, j, active_cnt = 0;

	len += sprintf(buf + len,
		"Device Mapper (dm) v1.0 (major %d)\n"
		"Minor  Name             State      Sectors   Size(KB)  Reads   Writes  Target Table\n",
		DM_MAJOR);

	for (i = 0; i < DM_MAX_DEVICES; i++) {
		struct dm_device *d = &dm_devs[i];
		if (!d->active)
			continue;
		active_cnt++;
		len += sprintf(buf + len, "%-6d %-16s %-10s %-9lu %-9lu %-7lu %-7lu ",
			i, d->name,
			d->suspended ? "SUSPENDED" : (d->ro ? "ACTIVE-RO" : "ACTIVE"),
			d->total_sectors, d->total_sectors >> 1,
			d->read_ios, d->write_ios);
		for (j = 0; j < d->num_targets; j++) {
			struct dm_target_spec *t = &d->targets[j];
			if (j > 0)
				len += sprintf(buf + len, "; ");
			if (t->type == DM_TARGET_LINEAR) {
				len += sprintf(buf + len, "%lu %lu linear %s %lu",
					t->start_sector, t->num_sectors,
					t->dev_name[0] ? t->dev_name : "/dev/hdc",
					t->offset_sector);
			} else if (t->type == DM_TARGET_CRYPT) {
				len += sprintf(buf + len, "%lu %lu crypt %s *** %lu %s %lu",
					t->start_sector, t->num_sectors,
					t->cipher[0] ? t->cipher : "chacha20",
					t->iv_offset,
					t->dev_name[0] ? t->dev_name : "/dev/hdc",
					t->offset_sector);
			} else if (t->type == DM_TARGET_VERITY) {
				len += sprintf(buf + len, "%lu %lu verity %s %s sha256 %.16s... (verified=%lu, corrupt=%lu)",
					t->start_sector, t->num_sectors,
					t->dev_name[0] ? t->dev_name : "/dev/hdd",
					t->dev_name[0] ? t->dev_name : "/dev/hdd",
					t->root_hash,
					t->verified_blocks, t->corrupt_blocks);
			} else if (t->type == DM_TARGET_STRIPED) {
				len += sprintf(buf + len, "%lu %lu striped 2 %lu %s %lu %s %lu",
					t->start_sector, t->num_sectors,
					t->chunk_sectors,
					t->dev_name[0] ? t->dev_name : "/dev/hdc",
					t->offset_sector,
					t->dev_name2[0] ? t->dev_name2 : "/dev/hdc",
					t->offset_sector2);
			} else {
				len += sprintf(buf + len, "%lu %lu %s",
					t->start_sector, t->num_sectors,
					dm_target_name(t->type));
			}
		}
		len += sprintf(buf + len, "\n");
	}
	if (active_cnt == 0)
		len += sprintf(buf + len, "(no active mapped devices)\n");
	return len;
}

void dm_notify_bdev_write(kdev_t bdev)
{
	int i, j, k;
	unsigned short raw_bdev = kdev_t_to_nr(bdev);
	extern struct inode *first_inode;
	extern int nr_inodes;

	for (i = 0; i < DM_MAX_DEVICES; i++) {
		if (!dm_devs[i].active)
			continue;
		for (j = 0; j < dm_devs[i].num_targets; j++) {
			if (dm_devs[i].targets[j].type == DM_TARGET_VERITY &&
			    dm_devs[i].targets[j].bdev == raw_bdev) {
				kdev_t dm_kdev = MKDEV(DM_MAJOR, i);
				struct inode *ino = first_inode;
				for (k = 0; k < nr_inodes && ino; k++, ino = ino->i_next) {
					if (ino->i_dev == dm_kdev)
						truncate_inode_pages(ino, 0);
				}
				invalidate_buffers(dm_kdev);
			}
		}
	}
}

/*
 * In-kernel dm-verity setup for /bin (/dev/hdd -> /dev/dm-0 -> /dev/mapper/verity_bin).
 * Called by init() in init/main.c immediately after mounting the root filesystem
 * and before executing /etc/init.
 */
int dm_setup_verity_bin(void)
{
	struct dm_device *dev = &dm_devs[0];
	struct dm_target_spec *t;
	struct six_verity_sb sb;
	kdev_t hdd_dev = MKDEV(HD_MAJOR, 192);

	if (six_disk_fd[3] < 0)
		return -ENODEV;

	if (dm_read_phys_1k(hdd_dev, VERITY_BIN_HASH_START_SECTOR, dm_verity_data_buf) != 0) {
		printk("device-mapper: verity: cannot read superblock on /dev/hdd\n");
		return -EIO;
	}
	memcpy(&sb, dm_verity_data_buf, sizeof(sb));
	if (memcmp(sb.magic, "verity\0\0", 8) != 0 || sb.version != 1) {
		printk("device-mapper: verity: no valid superblock on /dev/hdd\n");
		return -EINVAL;
	}
	if (memcmp(sb.root_hash, verity_bin_root_hash, 32) != 0) {
		printk("device-mapper: verity: superblock root hash mismatch against kernel trusted root hash!\n");
		return -EIO;
	}

	memset(dev, 0, sizeof(*dev));
	strcpy(dev->name, "verity_bin");
	dev->ro = 1;
	dev->num_targets = 1;
	dev->total_sectors = VERITY_BIN_DATA_SECTORS;
	memcpy(dev->verity_salt, sb.salt, 32);
	memcpy(dev->verity_root_hash, verity_bin_root_hash, 32);

	t = &dev->targets[0];
	t->start_sector = 0;
	t->num_sectors = VERITY_BIN_DATA_SECTORS;
	t->type = DM_TARGET_VERITY;
	t->bdev = (HD_MAJOR << 8) | 192;
	strcpy(t->dev_name, "/dev/hdd");
	t->offset_sector = 0;
	t->hash_start_sector = VERITY_BIN_HASH_START_SECTOR;
	strcpy(t->cipher, "sha256");
	strcpy(t->root_hash, VERITY_BIN_ROOT_HASH_HEX);

	/* Pre-verify block 0 and block 1 (ext4 superblock) before activating */
	if (dm_read_phys_1k(hdd_dev, 0, dm_verity_data_buf) != 0 ||
	    dm_verify_verity_block(dev, t, 0, dm_verity_data_buf) != 0 ||
	    dm_read_phys_1k(hdd_dev, 2, dm_verity_data_buf) != 0 ||
	    dm_verify_verity_block(dev, t, 1, dm_verity_data_buf) != 0) {
		printk("device-mapper: verity: initial Merkle tree verification FAILED on /dev/hdd!\n");
		memset(dev, 0, sizeof(*dev));
		return -EIO;
	}

	dev->active = 1;
	dev->suspended = 0;
	dm_sizes[0] = dev->total_sectors >> (BLOCK_SIZE_BITS - 9);
	dm_blocksizes[0] = 1024;
	set_device_ro(MKDEV(DM_MAJOR, 0), 1);

	printk("device-mapper: verity: SHA-256 Merkle root %.16s... verified on /dev/hdd\n",
	       VERITY_BIN_ROOT_HASH_HEX);
	printk("device-mapper: created /dev/dm-0 (verity_bin), %lu sectors (%lu KB, read-only)\n",
	       dev->total_sectors, dev->total_sectors >> 1);
	return 0;
}

int dm_init(void)
{
	int i;

	if (register_blkdev(DM_MAJOR, "dm", &dm_fops)) {
		printk("device-mapper: unable to register major %d\n", DM_MAJOR);
		return -1;
	}
	blk_dev[DM_MAJOR].request_fn = DEVICE_REQUEST;
	read_ahead[DM_MAJOR] = 8;
	memset(dm_devs, 0, sizeof(dm_devs));
	for (i = 0; i < 64; i++) {
		dm_sizes[i] = 0;
		dm_blocksizes[i] = 1024;
	}
	blk_size[DM_MAJOR] = dm_sizes;
	blksize_size[DM_MAJOR] = dm_blocksizes;
	printk("device-mapper: v1.0 initialized (major %d, targets: linear, crypt, verity, striped, zero, error)\n",
	       DM_MAJOR);
	return 0;
}
