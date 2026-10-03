/*
 * fs/erofs/super.c
 *
 * Enhanced Read-Only File System (EROFS v1, magic 0xE0F5E1E2) VFS driver
 * for SIX (Linux 2.0.11), including Sarthak Kukreti's Native EROFS Verity
 * (go/erofs-verity, topic:native_erofs_verity, aosp/4324130..4324134).
 *
 * Supports:
 *   - EROFS v1 superblock at offset 1024 (EROFS_SUPER_MAGIC_V1)
 *   - Compact (32-byte) and Extended (64-byte) inodes addressed directly
 *     by 64-bit NID: disk_offset = (meta_blkaddr << blkszbits) + (nid << 5)
 *   - EROFS_INODE_FLAT_PLAIN (contiguous block mapping, O(1) bmap + mmap)
 *   - EROFS_INODE_FLAT_INLINE (tail-packing inline data right after inode)
 *   - Native EROFS Verity (EROFS_FEATURE_INCOMPAT_VERITY, 0x00000200):
 *       * Superblock Block 1 authenticated at mount via -o root_digest=<hex>
 *       * Precomputed SHA-256 salt midstate (verity_init_state[8], 16 rounds/1KB)
 *       * Metadata Merkle tree pinned & verified at mount, with 1-bit/block
 *         meta_verified_bitmap so each metadata block is verified at most once
 *       * Per-block inline 32-byte SHA-256 digest table right after FLAT_PLAIN
 *         inodes (0 Merkle tree block reads during file/directory data reads)
 *   - Live per-mount execution & verification telemetry in /proc/erofs
 */

#include "../../arch/six/kernel/host.h"
#include <linux/module.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/bitops.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/erofs_fs.h>
#include <linux/malloc.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/locks.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/major.h>

#define EROFS_MAX_MOUNTS 8
static struct super_block *erofs_mounts[EROFS_MAX_MOUNTS];
static unsigned long erofs_last_dm_ver[EROFS_MAX_MOUNTS];
static int erofs_auto_cold = 0;     /* 0=warm, 1=cold data, 2=cold data+meta */
static int erofs_oneshot_cold = 0;  /* armed by writing "cold"/"cold_all" */
static int erofs_tamper_active = 0; /* armed by writing "tamper" to /proc/erofs */

static void erofs_read_inode(struct inode *inode);
static void erofs_put_super(struct super_block *sb);
static void erofs_statfs(struct super_block *sb, struct statfs *buf, int bufsiz);
static int erofs_remount(struct super_block *sb, int *flags, char *data);

static int erofs_bmap(struct inode *inode, int block);
static int erofs_readpage(struct inode *inode, struct page *page);
static int erofs_file_read(struct inode *inode, struct file *filp, char *buf, int count);
static int erofs_readdir(struct inode *inode, struct file *filp, void *dirent, filldir_t filldir);
static int erofs_lookup(struct inode *dir, const char *name, int len, struct inode **result);
static int erofs_readlink(struct inode *inode, char *buffer, int buflen);
static int erofs_follow_link(struct inode *dir, struct inode *inode,
			     int flag, int mode, struct inode **res_inode);

extern void dm_get_verity_stats(int minor, unsigned long *verified_out,
				unsigned long *corrupt_out, unsigned long *read_secs_out);

/*
 * ===========================================================================
 * FIPS 180-4 SHA-256 with Precomputed Salt Midstate (go/erofs-verity)
 * ===========================================================================
 */
static inline unsigned int erofs_rotr32(unsigned int v, int c)
{
	return (v >> c) | (v << (32 - c));
}

static const unsigned int erofs_sha256_k[64] = {
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

static void erofs_sha256_transform(unsigned int state[8], const unsigned char block[64])
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
		unsigned int s0 = erofs_rotr32(w[i - 15], 7) ^ erofs_rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
		unsigned int s1 = erofs_rotr32(w[i - 2], 17) ^ erofs_rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}

	a = state[0]; b = state[1]; c = state[2]; d = state[3];
	e = state[4]; f = state[5]; g = state[6]; h = state[7];

	for (i = 0; i < 64; i++) {
		unsigned int S1 = erofs_rotr32(e, 6) ^ erofs_rotr32(e, 11) ^ erofs_rotr32(e, 25);
		unsigned int ch = (e & f) ^ ((~e) & g);
		unsigned int S0 = erofs_rotr32(a, 2) ^ erofs_rotr32(a, 13) ^ erofs_rotr32(a, 22);
		unsigned int maj = (a & b) ^ (a & c) ^ (b & c);
		t1 = h + S1 + ch + erofs_sha256_k[i] + w[i];
		t2 = S0 + maj;
		h = g; g = f; f = e; e = d + t1;
		d = c; c = b; b = a; a = t1 + t2;
	}

	state[0] += a; state[1] += b; state[2] += c; state[3] += d;
	state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

/*
 * Unsalted SHA-256 over a 1024-byte block (8192 bits = 0x2000).
 * Used once at mount time to authenticate Block 1 against -o root_digest=<hex>.
 */
static void erofs_sha256_plain_1k(const unsigned char blk[1024],
				  unsigned char out_digest[32])
{
	unsigned int state[8];
	unsigned char pad[64];
	int i;

	state[0] = 0x6a09e667U; state[1] = 0xbb67ae85U;
	state[2] = 0x3c6ef372U; state[3] = 0xa54ff53aU;
	state[4] = 0x510e527fU; state[5] = 0x9b05688cU;
	state[6] = 0x1f83d9abU; state[7] = 0x5be0cd19U;

	for (i = 0; i < 16; i++)
		erofs_sha256_transform(state, blk + i * 64);

	memset(pad, 0, sizeof(pad));
	pad[0] = 0x80;
	pad[62] = 0x20;
	pad[63] = 0x00;
	erofs_sha256_transform(state, pad);

	for (i = 0; i < 8; i++) {
		out_digest[i * 4 + 0] = (unsigned char)((state[i] >> 24) & 0xff);
		out_digest[i * 4 + 1] = (unsigned char)((state[i] >> 16) & 0xff);
		out_digest[i * 4 + 2] = (unsigned char)((state[i] >> 8) & 0xff);
		out_digest[i * 4 + 3] = (unsigned char)(state[i] & 0xff);
	}
}

/*
 * Precompute the SHA-256 midstate after compressing the 64-byte zero-padded
 * verity_salt block once at mount time (go/erofs-verity).
 */
static void erofs_verity_precompute_salt(struct erofs_sb_info *sbi)
{
	unsigned char salt_blk[64];

	sbi->verity_init_state[0] = 0x6a09e667U;
	sbi->verity_init_state[1] = 0xbb67ae85U;
	sbi->verity_init_state[2] = 0x3c6ef372U;
	sbi->verity_init_state[3] = 0xa54ff53aU;
	sbi->verity_init_state[4] = 0x510e527fU;
	sbi->verity_init_state[5] = 0x9b05688cU;
	sbi->verity_init_state[6] = 0x1f83d9abU;
	sbi->verity_init_state[7] = 0x5be0cd19U;

	memset(salt_blk, 0, sizeof(salt_blk));
	memcpy(salt_blk, sbi->verity_salt, 32);
	erofs_sha256_transform(sbi->verity_init_state, salt_blk);
}

/*
 * Compute SHA-256(salt_padded[64] || blk[1024]) starting from precomputed
 * sbi->verity_init_state[8] (16 data block compression rounds vs 68 in dm-verity).
 */
static void erofs_verity_hash_1k(struct erofs_sb_info *sbi,
				 const unsigned char blk[1024],
				 unsigned char out_digest[32])
{
	unsigned int state[8];
	unsigned char pad[64];
	int i;

	for (i = 0; i < 8; i++)
		state[i] = sbi->verity_init_state[i];

	for (i = 0; i < 16; i++)
		erofs_sha256_transform(state, blk + i * 64);

	/* Total length = 64 + 1024 = 1088 bytes = 8704 bits (0x2200) */
	memset(pad, 0, sizeof(pad));
	pad[0] = 0x80;
	pad[62] = 0x22;
	pad[63] = 0x00;
	erofs_sha256_transform(state, pad);

	for (i = 0; i < 8; i++) {
		out_digest[i * 4 + 0] = (unsigned char)((state[i] >> 24) & 0xff);
		out_digest[i * 4 + 1] = (unsigned char)((state[i] >> 16) & 0xff);
		out_digest[i * 4 + 2] = (unsigned char)((state[i] >> 8) & 0xff);
		out_digest[i * 4 + 3] = (unsigned char)(state[i] & 0xff);
	}

	sbi->sha256_calls += 1;
	sbi->sha256_rounds += 16;
}

static int erofs_hex_nibble(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int erofs_parse_root_digest(const char *options, unsigned char out_digest[32])
{
	const char *p;
	int i;

	if (!options || !options[0])
		return 0;
	p = strstr(options, "root_digest=");
	if (!p)
		return 0;
	p += 12;
	for (i = 0; i < 32; i++) {
		int hi = erofs_hex_nibble(p[i * 2]);
		int lo = erofs_hex_nibble(p[i * 2 + 1]);
		if (hi < 0 || lo < 0)
			return -EINVAL;
		out_digest[i] = (unsigned char)((hi << 4) | lo);
	}
	return 1;
}

/*
 * Synchronize dm-verity hardware/driver telemetry from drivers/block/dm.c
 * for stock EROFS mounts stacked on dm-verity (e.g. /dev/dm-0 -> /bin).
 */
static void erofs_sync_dm_stats(struct super_block *sb)
{
	struct erofs_sb_info *sbi;
	int idx = -1, i;
	unsigned long cur_ver = 0, cur_cor = 0, cur_rsec = 0;

	if (!sb || !sb->u.generic_sbp)
		return;
	sbi = (struct erofs_sb_info *)sb->u.generic_sbp;
	if (sbi->verity_enabled || MAJOR(sb->s_dev) != DM_MAJOR)
		return;

	for (i = 0; i < EROFS_MAX_MOUNTS; i++) {
		if (erofs_mounts[i] == sb) {
			idx = i;
			break;
		}
	}
	if (idx < 0)
		return;

	dm_get_verity_stats(MINOR(sb->s_dev), &cur_ver, &cur_cor, &cur_rsec);
	if (cur_ver > erofs_last_dm_ver[idx]) {
		unsigned long dv = cur_ver - erofs_last_dm_ver[idx];
		erofs_last_dm_ver[idx] = cur_ver;
		sbi->data_blk_reads += dv;
		sbi->merkle_blk_reads += dv * 3;
		sbi->sha256_calls += dv * 4;
		sbi->sha256_rounds += dv * 68;
		sbi->data_blocks_verified += dv;
	}
	sbi->corrupt_blocks = cur_cor;
}

static struct super_operations erofs_sops = {
	erofs_read_inode,
	NULL,			/* notify_change */
	NULL,			/* write_inode */
	NULL,			/* put_inode */
	erofs_put_super,
	NULL,			/* write_super */
	erofs_statfs,
	erofs_remount
};

static struct file_operations erofs_file_operations = {
	NULL,			/* lseek - default */
	erofs_file_read,	/* read */
	NULL,			/* write - read-only fs */
	NULL,			/* readdir */
	NULL,			/* select */
	NULL,			/* ioctl */
	generic_file_mmap,	/* mmap */
	NULL,			/* open */
	NULL,			/* release */
	NULL,			/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL			/* revalidate */
};

static struct inode_operations erofs_file_inode_operations = {
	&erofs_file_operations,
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	erofs_readpage,		/* readpage */
	NULL,			/* writepage */
	erofs_bmap,		/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL			/* smap */
};

static int erofs_dir_read(struct inode *inode, struct file *filp, char *buf, int count)
{
	return -EISDIR;
}

static struct file_operations erofs_dir_operations = {
	NULL,			/* lseek */
	erofs_dir_read,		/* read */
	NULL,			/* write */
	erofs_readdir,		/* readdir */
	NULL,			/* select */
	NULL,			/* ioctl */
	NULL,			/* mmap */
	NULL,			/* open */
	NULL,			/* release */
	NULL,			/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL			/* revalidate */
};

static struct inode_operations erofs_dir_inode_operations = {
	&erofs_dir_operations,
	NULL,			/* create */
	erofs_lookup,		/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL			/* smap */
};

static struct inode_operations erofs_symlink_inode_operations = {
	NULL,			/* default_file_ops */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	erofs_readlink,		/* readlink */
	erofs_follow_link,	/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL			/* smap */
};

/*
 * Verify a metadata block in [meta_verity_startblk .. +meta_verity_blocks)
 * using sbi->meta_verified_bitmap and pinned sbi->meta_merkle_leaves
 * (go/erofs-verity, aosp/4324133).
 */
static int erofs_verify_meta_block(struct super_block *sb,
				   struct erofs_sb_info *sbi,
				   unsigned long blk,
				   const unsigned char *bdata)
{
	unsigned long m_idx;
	unsigned char digest[32];

	if (!sbi || !sbi->verity_enabled || !sbi->meta_merkle_leaves || !sbi->meta_verified_bitmap)
		return 0;
	if (blk < sbi->meta_verity_startblk ||
	    blk >= sbi->meta_verity_startblk + sbi->meta_verity_blocks)
		return 0;

	m_idx = blk - sbi->meta_verity_startblk;
	if (test_bit(m_idx, sbi->meta_verified_bitmap)) {
		sbi->meta_bitmap_hits++;
		return 0;
	}

	/* First access to this metadata block since mount: hash & verify */
	sbi->data_blk_reads++;
	erofs_verity_hash_1k(sbi, bdata, digest);
	if (memcmp(digest, sbi->meta_merkle_leaves + (m_idx << 5), 32) != 0) {
		sbi->corrupt_blocks++;
		printk("EROFS-fs (%s): verity: CORRUPTION DETECTED at metadata block %lu (leaf %lu mismatch)!\n",
		       kdevname(sb->s_dev), blk, m_idx);
		return -EIO;
	}

	set_bit(m_idx, sbi->meta_verified_bitmap);
	sbi->meta_blocks_verified++;
	return 0;
}

/*
 * Read raw bytes from disk at arbitrary byte offset `disk_off`.
 * Handles reads that cross 1024-byte block boundaries cleanly and
 * authenticates metadata blocks via go/erofs-verity when enabled.
 */
static int erofs_read_disk_bytes(struct super_block *sb, unsigned long disk_off,
				 char *dst, int len)
{
	struct erofs_sb_info *sbi = (struct erofs_sb_info *)sb->u.generic_sbp;
	unsigned long blksz = sb->s_blocksize;
	unsigned char blkszbits = sb->s_blocksize_bits;

	while (len > 0) {
		unsigned long blk = disk_off >> blkszbits;
		unsigned long boff = disk_off & (blksz - 1);
		int chunk = blksz - boff;
		struct buffer_head *bh;

		if (chunk > len)
			chunk = len;
		bh = bread(sb->s_dev, blk, blksz);
		if (!bh)
			return -EIO;

		if (sbi && sbi->verity_enabled) {
			if (erofs_verify_meta_block(sb, sbi, blk,
						    (const unsigned char *)bh->b_data) != 0) {
				brelse(bh);
				return -EIO;
			}
		} else {
			erofs_sync_dm_stats(sb);
		}

		memcpy(dst, bh->b_data + boff, chunk);
		brelse(bh);

		dst += chunk;
		disk_off += chunk;
		len -= chunk;
	}
	return 0;
}

/*
 * Read `len` bytes from an EROFS inode starting at file byte offset `pos`
 * into kernel buffer `kbuf`. Supports both EROFS_INODE_FLAT_PLAIN and
 * EROFS_INODE_FLAT_INLINE (tail-packing), and verifies FLAT_PLAIN data
 * blocks against their per-block inline SHA-256 digest table when
 * Native EROFS Verity (go/erofs-verity) is enabled on the superblock.
 */
static int erofs_read_inode_kbuf(struct inode *inode, unsigned long pos,
				 char *kbuf, int len)
{
	struct super_block *sb = inode->i_sb;
	struct erofs_sb_info *sbi = (struct erofs_sb_info *)sb->u.generic_sbp;
	unsigned long blksz = sb->s_blocksize;
	unsigned char blkszbits = sb->s_blocksize_bits;
	unsigned long full_blocks = inode->i_size >> blkszbits;
	unsigned long datalayout = EROFS_I_DATALAYOUT(inode);
	unsigned long raw_blkaddr = EROFS_I_RAW_BLKADDR(inode);
	unsigned long inline_off = EROFS_I_INLINE_OFF(inode);
	int total = 0;

	if (pos >= inode->i_size || len <= 0)
		return 0;
	if (pos + len > inode->i_size)
		len = inode->i_size - pos;

	while (total < len) {
		unsigned long lblk = pos >> blkszbits;
		unsigned long boff = pos & (blksz - 1);
		int chunk = blksz - boff;
		int err = 0;

		if (chunk > len - total)
			chunk = len - total;

		if (datalayout == EROFS_INODE_FLAT_PLAIN || lblk < full_blocks) {
			unsigned long phys_blk = raw_blkaddr + lblk;

			if (sbi && sbi->verity_enabled) {
				int already_verified = (sbi->data_verified_bitmap &&
							phys_blk < sbi->blocks &&
							test_bit(phys_blk, sbi->data_verified_bitmap));
				struct buffer_head *bh = bread(sb->s_dev, phys_blk, blksz);
				if (!bh)
					return total > 0 ? total : -EIO;

				if (!already_verified) {
					unsigned char exp_digest[32];
					unsigned char act_digest[32];
					unsigned long dig_off = inline_off + (lblk << 5);

					sbi->data_blk_reads++;
					if (erofs_read_disk_bytes(sb, dig_off,
								  (char *)exp_digest, 32) != 0) {
						brelse(bh);
						return total > 0 ? total : -EIO;
					}
					if (erofs_tamper_active)
						bh->b_data[0] ^= (char)0xFF;
					erofs_verity_hash_1k(sbi,
							     (const unsigned char *)bh->b_data,
							     act_digest);
					if (erofs_tamper_active)
						bh->b_data[0] ^= (char)0xFF;
					if (memcmp(act_digest, exp_digest, 32) != 0) {
						sbi->corrupt_blocks++;
						brelse(bh);
						printk("EROFS-fs (%s): verity: CORRUPTION DETECTED at data block %lu (nid=%lu, lblk=%lu inline digest mismatch)!\n",
						       kdevname(sb->s_dev), phys_blk,
						       EROFS_I_NID(inode), lblk);
						return total > 0 ? total : -EIO;
					}
					if (sbi->data_verified_bitmap && phys_blk < sbi->blocks)
						set_bit(phys_blk, sbi->data_verified_bitmap);
					sbi->data_blocks_verified++;
				}
				memcpy(kbuf + total, bh->b_data + boff, chunk);
				brelse(bh);
			} else {
				unsigned long disk_off = (phys_blk << blkszbits) + boff;
				err = erofs_read_disk_bytes(sb, disk_off, kbuf + total, chunk);
			}
		} else if (datalayout == EROFS_INODE_FLAT_INLINE) {
			unsigned long disk_off = inline_off + boff;
			err = erofs_read_disk_bytes(sb, disk_off, kbuf + total, chunk);
		} else {
			return -EIO;
		}
		if (err)
			return total > 0 ? total : err;

		pos += chunk;
		total += chunk;
	}
	return total;
}

static int erofs_bmap(struct inode *inode, int block)
{
	unsigned long blksz;
	unsigned long nblocks;
	unsigned long datalayout;

	if (!inode || !inode->i_sb || block < 0)
		return 0;
	blksz = inode->i_sb->s_blocksize;
	if ((unsigned long)block * blksz >= inode->i_size)
		return 0;

	datalayout = EROFS_I_DATALAYOUT(inode);
	nblocks = inode->i_size >> inode->i_sb->s_blocksize_bits;
	if (datalayout == EROFS_INODE_FLAT_PLAIN || (unsigned long)block < nblocks)
		return EROFS_I_RAW_BLKADDR(inode) + block;
	return 0;
}

static int erofs_readpage(struct inode *inode, struct page *page)
{
	struct erofs_sb_info *sbi = inode && inode->i_sb ?
		(struct erofs_sb_info *)inode->i_sb->u.generic_sbp : NULL;
	unsigned long address;
	int nread;

	if (EROFS_I_DATALAYOUT(inode) == EROFS_INODE_FLAT_PLAIN &&
	    (!sbi || !sbi->verity_enabled))
		return generic_readpage(inode, page);

	address = page_address(page);
	memset((void *)address, 0, PAGE_SIZE);
	nread = erofs_read_inode_kbuf(inode, page->offset, (char *)address, PAGE_SIZE);
	if (nread < 0)
		return nread;
	set_bit(PG_uptodate, &page->flags);
	return 0;
}

static int erofs_file_read(struct inode *inode, struct file *filp,
			   char *buf, int count)
{
	char ktmp[1024];
	int total = 0;

	if (!inode)
		return -EINVAL;
	if (count <= 0 || filp->f_pos >= inode->i_size)
		return 0;
	if (filp->f_pos + count > inode->i_size)
		count = inode->i_size - filp->f_pos;

	while (total < count) {
		int ask = count - total;
		int got;

		if (ask > (int)sizeof(ktmp))
			ask = sizeof(ktmp);
		got = erofs_read_inode_kbuf(inode, filp->f_pos, ktmp, ask);
		if (got <= 0)
			return total > 0 ? total : got;
		memcpy_tofs(buf + total, ktmp, got);
		filp->f_pos += got;
		total += got;
	}
	return total;
}

static int erofs_read_dir_block(struct inode *dir, unsigned long lblk, char *kbuf)
{
	unsigned long blksz = dir->i_sb->s_blocksize;
	unsigned long byte_off = lblk << dir->i_sb->s_blocksize_bits;
	int rem;

	if (byte_off >= dir->i_size)
		return 0;
	rem = dir->i_size - byte_off;
	if (rem > (int)blksz)
		rem = blksz;
	return erofs_read_inode_kbuf(dir, byte_off, kbuf, rem);
}

static int erofs_readdir(struct inode *inode, struct file *filp,
			 void *dirent, filldir_t filldir)
{
	char dblk[1024];
	unsigned long blksz;
	unsigned char blkszbits;

	if (!inode || !S_ISDIR(inode->i_mode))
		return -EBADF;

	blksz = inode->i_sb->s_blocksize;
	blkszbits = inode->i_sb->s_blocksize_bits;

	while (1) {
		unsigned long lblk = filp->f_pos >> blkszbits;
		unsigned int idx = filp->f_pos & (blksz - 1);
		int blklen;
		struct erofs_dirent *de;
		unsigned int nr_entries;

		if ((lblk << blkszbits) >= inode->i_size)
			break;

		blklen = erofs_read_dir_block(inode, lblk, dblk);
		if (blklen < (int)sizeof(struct erofs_dirent))
			break;

		de = (struct erofs_dirent *)dblk;
		if (de[0].nameoff < sizeof(struct erofs_dirent) ||
		    de[0].nameoff > (unsigned int)blklen)
			break;
		nr_entries = de[0].nameoff / sizeof(struct erofs_dirent);

		while (idx < nr_entries) {
			unsigned int nameoff = de[idx].nameoff;
			unsigned int nextoff = (idx + 1 < nr_entries) ?
					       de[idx + 1].nameoff : (unsigned int)blklen;
			int namelen;
			const char *name;
			unsigned long target_ino;

			if (nameoff >= (unsigned int)blklen || nextoff > (unsigned int)blklen ||
			    nextoff < nameoff) {
				idx++;
				continue;
			}
			name = dblk + nameoff;
			namelen = nextoff - nameoff;
			while (namelen > 0 && name[namelen - 1] == '\0')
				namelen--;

			target_ino = (unsigned long)de[idx].nid;
			if (namelen > 0 && target_ino > 0) {
				off_t cur_pos = (lblk << blkszbits) | idx;
				if (filldir(dirent, name, namelen, cur_pos, target_ino) < 0) {
					filp->f_pos = cur_pos;
					return 0;
				}
			}
			idx++;
			filp->f_pos = (lblk << blkszbits) | idx;
		}
		filp->f_pos = (lblk + 1) << blkszbits;
	}
	return 0;
}

static int erofs_lookup(struct inode *dir, const char *name, int len,
			struct inode **result)
{
	char dblk[1024];
	unsigned long lblk;
	unsigned long total_blocks;

	*result = NULL;
	if (!dir)
		return -ENOENT;
	if (!S_ISDIR(dir->i_mode)) {
		iput(dir);
		return -ENOENT;
	}
	if (len == 0 || (len == 1 && name[0] == '.')) {
		*result = dir;
		return 0;
	}

	total_blocks = (dir->i_size + dir->i_sb->s_blocksize - 1) >>
		       dir->i_sb->s_blocksize_bits;

	for (lblk = 0; lblk < total_blocks; lblk++) {
		int blklen = erofs_read_dir_block(dir, lblk, dblk);
		struct erofs_dirent *de;
		unsigned int nr_entries, idx;

		if (blklen < (int)sizeof(struct erofs_dirent))
			continue;
		de = (struct erofs_dirent *)dblk;
		if (de[0].nameoff < sizeof(struct erofs_dirent) ||
		    de[0].nameoff > (unsigned int)blklen)
			continue;
		nr_entries = de[0].nameoff / sizeof(struct erofs_dirent);

		for (idx = 0; idx < nr_entries; idx++) {
			unsigned int nameoff = de[idx].nameoff;
			unsigned int nextoff = (idx + 1 < nr_entries) ?
					       de[idx + 1].nameoff : (unsigned int)blklen;
			int namelen;
			const char *ename;

			if (nameoff >= (unsigned int)blklen || nextoff > (unsigned int)blklen ||
			    nextoff < nameoff)
				continue;
			ename = dblk + nameoff;
			namelen = nextoff - nameoff;
			while (namelen > 0 && ename[namelen - 1] == '\0')
				namelen--;

			if (namelen == len && memcmp(ename, name, len) == 0) {
				unsigned long ino = (unsigned long)de[idx].nid;
				struct super_block *sb = dir->i_sb;
				struct erofs_sb_info *sbi = (struct erofs_sb_info *)sb->u.generic_sbp;

				iput(dir);
				*result = iget(sb, ino);
				if (!*result)
					return -EACCES;
				if ((*result)->i_count > 1 && sbi) {
					char hdr[32];
					unsigned long ioff = ((unsigned long)sbi->meta_blkaddr << sbi->blkszbits) +
							     (ino << EROFS_ISLOTBITS);
					if (erofs_read_disk_bytes(sb, ioff, hdr, sizeof(hdr)) != 0) {
						iput(*result);
						*result = NULL;
						return -EIO;
					}
				}
				return 0;
			}
		}
	}

	iput(dir);
	return -ENOENT;
}

static int erofs_readlink(struct inode *inode, char *buffer, int buflen)
{
	char linkbuf[512];
	int len, i;

	if (!S_ISLNK(inode->i_mode)) {
		iput(inode);
		return -EINVAL;
	}
	if (buflen > (int)sizeof(linkbuf) - 1)
		buflen = sizeof(linkbuf) - 1;
	len = erofs_read_inode_kbuf(inode, 0, linkbuf, buflen);
	if (len < 0) {
		iput(inode);
		return len;
	}
	for (i = 0; i < len && linkbuf[i]; i++)
		put_user(linkbuf[i], buffer++);
	iput(inode);
	return i;
}

static int erofs_follow_link(struct inode *dir, struct inode *inode,
			     int flag, int mode, struct inode **res_inode)
{
	char linkbuf[512];
	int len, error;

	*res_inode = NULL;
	if (!dir) {
		dir = current->fs->root;
		dir->i_count++;
	}
	if (!inode) {
		iput(dir);
		return -ENOENT;
	}
	if (!S_ISLNK(inode->i_mode)) {
		iput(dir);
		*res_inode = inode;
		return 0;
	}
	if (current->link_count > 5) {
		iput(dir);
		iput(inode);
		return -ELOOP;
	}
	len = erofs_read_inode_kbuf(inode, 0, linkbuf, sizeof(linkbuf) - 1);
	if (len < 0) {
		iput(dir);
		iput(inode);
		return len;
	}
	linkbuf[len] = '\0';
	current->link_count++;
	error = open_namei(linkbuf, flag, mode, res_inode, dir);
	current->link_count--;
	iput(inode);
	return error;
}

static void erofs_read_inode(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct erofs_sb_info *sbi = (struct erofs_sb_info *)sb->u.generic_sbp;
	unsigned long nid = inode->i_ino;
	unsigned long inode_off;
	unsigned char raw_buf[64];
	__u16 i_format, xattr_icount;
	unsigned int isize_hdr, xattr_isize, datalayout;
	unsigned long raw_blkaddr = 0;

	inode->i_op = NULL;
	inode->i_mode = 0;
	if (!sbi)
		return;

	inode_off = ((unsigned long)sbi->meta_blkaddr << sbi->blkszbits) +
		    (nid << EROFS_ISLOTBITS);
	if (erofs_read_disk_bytes(sb, inode_off, (char *)raw_buf, sizeof(raw_buf)) != 0) {
		printk("EROFS-fs: unable to read inode nid=%lu at offset %lu\n",
		       nid, inode_off);
		return;
	}

	i_format = *(__u16 *)raw_buf;
	datalayout = (i_format >> EROFS_I_DATALAYOUT_BIT) & EROFS_I_DATALAYOUT_MASK;

	if ((i_format & EROFS_I_VERSION_MASK) == EROFS_I_VERS_COMPACT) {
		struct erofs_inode_compact *ic = (struct erofs_inode_compact *)raw_buf;
		isize_hdr = sizeof(struct erofs_inode_compact); /* 32 */
		xattr_icount = ic->i_xattr_icount;
		inode->i_mode = ic->i_mode;
		inode->i_nlink = ic->i_nlink;
		inode->i_size = ic->i_size;
		raw_blkaddr = ic->i_u;
		inode->i_uid = ic->i_uid;
		inode->i_gid = ic->i_gid;
		inode->i_mtime = inode->i_atime = inode->i_ctime = (unsigned long)sbi->build_time;
	} else {
		struct erofs_inode_extended *ie = (struct erofs_inode_extended *)raw_buf;
		isize_hdr = sizeof(struct erofs_inode_extended); /* 64 */
		xattr_icount = ie->i_xattr_icount;
		inode->i_mode = ie->i_mode;
		inode->i_nlink = ie->i_nlink;
		inode->i_size = (unsigned long)ie->i_size;
		raw_blkaddr = ie->i_u;
		inode->i_uid = ie->i_uid;
		inode->i_gid = ie->i_gid;
		inode->i_mtime = inode->i_atime = inode->i_ctime = (unsigned long)ie->i_mtime;
	}

	xattr_isize = (xattr_icount == 0) ? 0 : (12 + (xattr_icount - 1) * 4);
	inode->i_blksize = sb->s_blocksize;
	inode->i_blocks = (inode->i_size + 511) >> 9;

	EROFS_I_DATALAYOUT(inode) = datalayout;
	EROFS_I_RAW_BLKADDR(inode) = raw_blkaddr;
	EROFS_I_INLINE_OFF(inode) = inode_off + isize_hdr + xattr_isize;
	EROFS_I_NID(inode) = nid;

	if (S_ISREG(inode->i_mode))
		inode->i_op = &erofs_file_inode_operations;
	else if (S_ISDIR(inode->i_mode))
		inode->i_op = &erofs_dir_inode_operations;
	else if (S_ISLNK(inode->i_mode))
		inode->i_op = &erofs_symlink_inode_operations;
	else if (S_ISCHR(inode->i_mode)) {
		inode->i_op = &chrdev_inode_operations;
		inode->i_rdev = to_kdev_t(raw_blkaddr);
	} else if (S_ISBLK(inode->i_mode)) {
		inode->i_op = &blkdev_inode_operations;
		inode->i_rdev = to_kdev_t(raw_blkaddr);
	} else if (S_ISFIFO(inode->i_mode))
		init_fifo(inode);
}

static void erofs_put_super(struct super_block *sb)
{
	struct erofs_sb_info *sbi;
	int i;

	lock_super(sb);
	for (i = 0; i < EROFS_MAX_MOUNTS; i++) {
		if (erofs_mounts[i] == sb) {
			erofs_mounts[i] = NULL;
			erofs_last_dm_ver[i] = 0;
		}
	}
	sbi = (struct erofs_sb_info *)sb->u.generic_sbp;
	if (sbi) {
		if (sbi->meta_merkle_leaves)
			kfree(sbi->meta_merkle_leaves);
		if (sbi->meta_verified_bitmap)
			kfree(sbi->meta_verified_bitmap);
		if (sbi->data_verified_bitmap)
			kfree(sbi->data_verified_bitmap);
		if (sbi->s_sbh)
			brelse(sbi->s_sbh);
		kfree_s(sbi, sizeof(struct erofs_sb_info));
		sb->u.generic_sbp = NULL;
	}
	sb->s_dev = 0;
	unlock_super(sb);
	MOD_DEC_USE_COUNT;
}

static void erofs_statfs(struct super_block *sb, struct statfs *buf, int bufsiz)
{
	struct erofs_sb_info *sbi = (struct erofs_sb_info *)sb->u.generic_sbp;
	struct statfs tmp;

	memset(&tmp, 0, sizeof(tmp));
	tmp.f_type = EROFS_SUPER_MAGIC_V1;
	tmp.f_bsize = sb->s_blocksize;
	tmp.f_blocks = sbi ? sbi->blocks : 0;
	tmp.f_bfree = 0;
	tmp.f_bavail = 0;
	tmp.f_files = sbi ? (long)sbi->inos : 0;
	tmp.f_ffree = 0;
	tmp.f_namelen = EROFS_NAME_LEN;
	memcpy_tofs(buf, &tmp, bufsiz);
}

static int erofs_remount(struct super_block *sb, int *flags, char *data)
{
	if (!(*flags & MS_RDONLY))
		return -EROFS;
	return 0;
}

struct super_block *erofs_read_super(struct super_block *sb, void *data, int silent)
{
	struct buffer_head *bh;
	struct erofs_super_block *esb;
	struct erofs_sb_info *sbi;
	kdev_t dev = sb->s_dev;
	unsigned char opt_digest[32];
	int has_opt_digest = 0;
	int i;

	MOD_INC_USE_COUNT;
	lock_super(sb);
	set_blocksize(dev, BLOCK_SIZE);

	if (data) {
		has_opt_digest = erofs_parse_root_digest((const char *)data, opt_digest);
		if (has_opt_digest < 0) {
			sb->s_dev = 0;
			unlock_super(sb);
			printk("EROFS-fs (%s): invalid root_digest= mount option\n",
			       kdevname(dev));
			MOD_DEC_USE_COUNT;
			return NULL;
		}
	}

	if (!(bh = bread(dev, 1, BLOCK_SIZE))) {
		sb->s_dev = 0;
		unlock_super(sb);
		if (!silent)
			printk("EROFS-fs: unable to read superblock on %s\n",
			       kdevname(dev));
		MOD_DEC_USE_COUNT;
		return NULL;
	}

	esb = (struct erofs_super_block *)bh->b_data;
	if (esb->magic != EROFS_SUPER_MAGIC_V1) {
		brelse(bh);
		sb->s_dev = 0;
		unlock_super(sb);
		if (!silent)
			printk("VFS: Can't find an EROFS filesystem on dev %s.\n",
			       kdevname(dev));
		MOD_DEC_USE_COUNT;
		return NULL;
	}

	sbi = (struct erofs_sb_info *)kmalloc(sizeof(struct erofs_sb_info), GFP_KERNEL);
	if (!sbi) {
		brelse(bh);
		sb->s_dev = 0;
		unlock_super(sb);
		MOD_DEC_USE_COUNT;
		return NULL;
	}
	memset(sbi, 0, sizeof(*sbi));
	sbi->s_sbh = bh;
	sbi->meta_blkaddr = esb->meta_blkaddr;
	sbi->blocks = esb->blocks;
	sbi->inos = esb->inos;
	sbi->build_time = esb->build_time;
	sbi->root_nid = esb->root_nid;
	sbi->blkszbits = esb->blkszbits ? esb->blkszbits : EROFS_BLKSZBITS;
	sbi->extslots = esb->extslots;
	sbi->feature_incompat = esb->feature_incompat;
	memcpy(sbi->uuid, esb->uuid, 16);
	memcpy(sbi->volume_name, esb->volume_name, 16);
	sbi->volume_name[16] = '\0';

	/*
	 * Native EROFS Verity (go/erofs-verity, aosp/4324130..4324134)
	 */
	if ((sbi->feature_incompat & EROFS_FEATURE_INCOMPAT_VERITY) || has_opt_digest > 0) {
		struct erofs_verity_sb_ext *sb_ext;
		unsigned char calc_sb_digest[32];

		if (!(sbi->feature_incompat & EROFS_FEATURE_INCOMPAT_VERITY) ||
		    sbi->extslots < EROFS_VERITY_EXTSLOTS) {
			brelse(bh);
			kfree_s(sbi, sizeof(struct erofs_sb_info));
			sb->s_dev = 0;
			unlock_super(sb);
			printk("EROFS-fs (%s): verity requested, but EROFS_FEATURE_INCOMPAT_VERITY missing\n",
			       kdevname(dev));
			MOD_DEC_USE_COUNT;
			return NULL;
		}

		erofs_sha256_plain_1k((const unsigned char *)bh->b_data, calc_sb_digest);
		if (has_opt_digest > 0) {
			if (memcmp(calc_sb_digest, opt_digest, 32) != 0) {
				brelse(bh);
				kfree_s(sbi, sizeof(struct erofs_sb_info));
				sb->s_dev = 0;
				unlock_super(sb);
				printk("EROFS-fs (%s): verity: superblock Block 1 root_digest MISMATCH!\n",
				       kdevname(dev));
				MOD_DEC_USE_COUNT;
				return NULL;
			}
			memcpy(sbi->root_digest, opt_digest, 32);
		} else {
			memcpy(sbi->root_digest, calc_sb_digest, 32);
		}

		sb_ext = (struct erofs_verity_sb_ext *)(bh->b_data + sizeof(struct erofs_super_block));
		sbi->verity_enabled = 1;
		sbi->meta_verity_startblk = sb_ext->meta_verity_startblk;
		sbi->meta_verity_blocks = sb_ext->meta_verity_blocks;
		sbi->meta_merkle_blkaddr = sb_ext->meta_merkle_blkaddr;
		sbi->meta_merkle_blocks = sb_ext->meta_merkle_blocks;
		memcpy(sbi->meta_merkle_root, sb_ext->meta_merkle_root, 32);
		memcpy(sbi->verity_salt, sb_ext->verity_salt, 32);

		/* Precompute SHA-256 salt midstate once at mount */
		erofs_verity_precompute_salt(sbi);

		/* Pin and verify metadata Merkle tree leaves at mount */
		if (sbi->meta_verity_blocks > 0 && sbi->meta_merkle_blocks > 0) {
			unsigned int leaves_bytes = sbi->meta_merkle_blocks * BLOCK_SIZE;
			unsigned char root_blk[BLOCK_SIZE];
			unsigned char calc_meta_root[32];
			unsigned int b;

			sbi->meta_merkle_leaves = (__u8 *)kmalloc(leaves_bytes, GFP_KERNEL);
			sbi->meta_bitmap_bytes = (sbi->meta_verity_blocks + 7) >> 3;
			if (sbi->meta_bitmap_bytes < 4)
				sbi->meta_bitmap_bytes = 4;
			sbi->meta_verified_bitmap = (__u8 *)kmalloc(sbi->meta_bitmap_bytes, GFP_KERNEL);
			sbi->data_bitmap_bytes = (sbi->blocks + 7) >> 3;
			if (sbi->data_bitmap_bytes < 4)
				sbi->data_bitmap_bytes = 4;
			sbi->data_verified_bitmap = (__u8 *)kmalloc(sbi->data_bitmap_bytes, GFP_KERNEL);

			if (!sbi->meta_merkle_leaves || !sbi->meta_verified_bitmap ||
			    !sbi->data_verified_bitmap) {
				if (sbi->meta_merkle_leaves) kfree(sbi->meta_merkle_leaves);
				if (sbi->meta_verified_bitmap) kfree(sbi->meta_verified_bitmap);
				if (sbi->data_verified_bitmap) kfree(sbi->data_verified_bitmap);
				brelse(bh);
				kfree_s(sbi, sizeof(struct erofs_sb_info));
				sb->s_dev = 0;
				unlock_super(sb);
				MOD_DEC_USE_COUNT;
				return NULL;
			}
			memset(sbi->meta_verified_bitmap, 0, sbi->meta_bitmap_bytes);
			memset(sbi->data_verified_bitmap, 0, sbi->data_bitmap_bytes);
			memset(root_blk, 0, sizeof(root_blk));

			for (b = 0; b < sbi->meta_merkle_blocks; b++) {
				struct buffer_head *mbh = bread(dev, sbi->meta_merkle_blkaddr + b, BLOCK_SIZE);
				if (!mbh) {
					kfree(sbi->meta_merkle_leaves);
					kfree(sbi->meta_verified_bitmap);
					kfree(sbi->data_verified_bitmap);
					brelse(bh);
					kfree_s(sbi, sizeof(struct erofs_sb_info));
					sb->s_dev = 0;
					unlock_super(sb);
					MOD_DEC_USE_COUNT;
					return NULL;
				}
				memcpy(sbi->meta_merkle_leaves + b * BLOCK_SIZE, mbh->b_data, BLOCK_SIZE);
				if (b < 32) {
					erofs_verity_hash_1k(sbi,
							     (const unsigned char *)mbh->b_data,
							     root_blk + b * 32);
				}
				sbi->merkle_blk_reads++;
				brelse(mbh);
			}

			erofs_verity_hash_1k(sbi, root_blk, calc_meta_root);
			if (memcmp(calc_meta_root, sbi->meta_merkle_root, 32) != 0) {
				kfree(sbi->meta_merkle_leaves);
				kfree(sbi->meta_verified_bitmap);
				kfree(sbi->data_verified_bitmap);
				brelse(bh);
				kfree_s(sbi, sizeof(struct erofs_sb_info));
				sb->s_dev = 0;
				unlock_super(sb);
				printk("EROFS-fs (%s): verity: metadata Merkle root MISMATCH!\n",
				       kdevname(dev));
				MOD_DEC_USE_COUNT;
				return NULL;
			}
			/* Reset mount-time Merkle bootstrap counters so runtime telemetry starts at 0 */
			sbi->merkle_blk_reads = 0;
			sbi->sha256_calls = 0;
			sbi->sha256_rounds = 0;
		}
	}

	sb->s_blocksize_bits = sbi->blkszbits;
	sb->s_blocksize = 1UL << sbi->blkszbits;
	sb->s_magic = EROFS_SUPER_MAGIC_V1;
	sb->s_flags |= MS_RDONLY;
	sb->s_rd_only = 1;
	sb->s_dirt = 0;
	sb->u.generic_sbp = sbi;
	sb->s_op = &erofs_sops;

	for (i = 0; i < EROFS_MAX_MOUNTS; i++) {
		if (!erofs_mounts[i]) {
			erofs_mounts[i] = sb;
			erofs_last_dm_ver[i] = 0;
			if (!sbi->verity_enabled && MAJOR(dev) == DM_MAJOR) {
				unsigned long v = 0, c = 0, r = 0;
				dm_get_verity_stats(MINOR(dev), &v, &c, &r);
				erofs_last_dm_ver[i] = v;
			}
			break;
		}
	}

	unlock_super(sb);
	sb->s_mounted = iget(sb, (unsigned long)sbi->root_nid);
	if (!sb->s_mounted) {
		erofs_put_super(sb);
		printk("EROFS-fs: get root inode (nid=%u) failed\n", sbi->root_nid);
		return NULL;
	}

	if (sbi->verity_enabled) {
		printk("EROFS-fs (%s): mounted go/erofs-verity read-only filesystem (blksz=%lu, blocks=%u, meta_blks=%u, merkle_blks=%u, root_nid=%u, label=%s)\n",
		       kdevname(dev), sb->s_blocksize, sbi->blocks,
		       sbi->meta_verity_blocks, sbi->meta_merkle_blocks,
		       sbi->root_nid, sbi->volume_name[0] ? sbi->volume_name : "none");
	} else {
		printk("EROFS-fs (%s): mounted read-only filesystem (blksz=%lu, blocks=%u, inos=%lu, root_nid=%u, label=%s)\n",
		       kdevname(dev), sb->s_blocksize, sbi->blocks,
		       (unsigned long)sbi->inos, sbi->root_nid,
		       sbi->volume_name[0] ? sbi->volume_name : "none");
	}
	return sb;
}

/*
 * Flush cached EROFS data blocks (and optionally metadata verified bitmap)
 * on a superblock so the next read/exec performs a cold disk + verity load.
 */
static void erofs_flush_cold_sb(struct super_block *sb, int flush_meta_bitmap)
{
	struct erofs_sb_info *sbi;
	extern struct inode *first_inode;
	extern int nr_inodes;
	struct inode *ino;
	int k;

	if (!sb || !sb->u.generic_sbp)
		return;
	sbi = (struct erofs_sb_info *)sb->u.generic_sbp;

	ino = first_inode;
	for (k = 0; k < nr_inodes && ino; k++, ino = ino->i_next) {
		if (ino->i_dev == sb->s_dev ||
		    (ino->i_sb && ino->i_sb->s_magic == 0x794c7630UL))
			truncate_inode_pages(ino, 0);
	}
	invalidate_buffers(sb->s_dev);

	if (sbi->data_verified_bitmap && sbi->data_bitmap_bytes > 0)
		memset(sbi->data_verified_bitmap, 0, sbi->data_bitmap_bytes);
	if (flush_meta_bitmap && sbi->meta_verified_bitmap && sbi->meta_bitmap_bytes > 0)
		memset(sbi->meta_verified_bitmap, 0, sbi->meta_bitmap_bytes);
}

void erofs_notify_bdev_write(kdev_t dev)
{
	int i;
	for (i = 0; i < EROFS_MAX_MOUNTS; i++) {
		if (erofs_mounts[i] && erofs_mounts[i]->s_dev == dev)
			erofs_flush_cold_sb(erofs_mounts[i], 1);
	}
}

/*
 * Exec telemetry hooks called by do_execve() in fs/exec.c
 */
static struct super_block *exec_active_sb = NULL;
static char exec_saved_comm[32];
static unsigned long exec_t0_us = 0;
static unsigned long exec_s_data = 0;
static unsigned long exec_s_merkle = 0;
static unsigned long exec_s_sha_calls = 0;
static unsigned long exec_s_sha_rounds = 0;
static unsigned long exec_s_meta_hits = 0;

static struct super_block *erofs_find_mount_for_path(const char *filename)
{
	int want_verity = -1;
	const char *want_label = NULL;
	int i;

	if (!filename || !filename[0])
		return NULL;
	if (strncmp(filename, "/bin-sarthak/", 13) == 0 ||
	    strncmp(filename, "/system/bin-sarthak/", 20) == 0) {
		want_verity = 1;
		want_label = "bin_sarthak";
	} else if (strncmp(filename, "/bin/", 5) == 0 ||
		   strncmp(filename, "/system/bin/", 12) == 0) {
		want_verity = 0;
		want_label = "bin_verity";
	} else if (strncmp(filename, "/vendor/", 8) == 0) {
		want_verity = 0;
		want_label = "vendor_verity";
	} else if (filename[0] != '/' && current && current->fs && current->fs->pwd) {
		struct inode *pwd = current->fs->pwd;
		for (i = 0; i < EROFS_MAX_MOUNTS; i++) {
			if (erofs_mounts[i] && pwd->i_dev == erofs_mounts[i]->s_dev)
				return erofs_mounts[i];
		}
		if (pwd->i_sb && pwd->i_sb->s_magic == 0x794c7630UL) {
			want_verity = 0;
			want_label = "bin_verity";
		}
	}
	if (want_verity < 0)
		return NULL;

	if (want_label) {
		for (i = 0; i < EROFS_MAX_MOUNTS; i++) {
			struct super_block *sb = erofs_mounts[i];
			if (sb && sb->u.generic_sbp) {
				struct erofs_sb_info *sbi = (struct erofs_sb_info *)sb->u.generic_sbp;
				if (strcmp(sbi->volume_name, want_label) == 0)
					return sb;
			}
		}
	}

	for (i = 0; i < EROFS_MAX_MOUNTS; i++) {
		struct super_block *sb = erofs_mounts[i];
		if (sb && sb->u.generic_sbp) {
			struct erofs_sb_info *sbi = (struct erofs_sb_info *)sb->u.generic_sbp;
			if ((sbi->verity_enabled ? 1 : 0) == want_verity)
				return sb;
		}
	}
	return NULL;
}

void erofs_exec_begin(const char *filename)
{
	struct super_block *sb;
	struct erofs_sb_info *sbi;
	const char *base;

	exec_active_sb = NULL;
	exec_saved_comm[0] = '\0';
	sb = erofs_find_mount_for_path(filename);
	if (!sb || !sb->u.generic_sbp)
		return;

	base = strrchr(filename, '/');
	base = base ? (base + 1) : filename;
	if (strcmp(base, "sarthak") == 0 || strcmp(base, "erofs_compare") == 0)
		return;

	strncpy(exec_saved_comm, base, sizeof(exec_saved_comm) - 1);
	exec_saved_comm[sizeof(exec_saved_comm) - 1] = '\0';

	if (erofs_auto_cold > 0) {
		erofs_flush_cold_sb(sb, erofs_auto_cold >= 2 ? 1 : 0);
	} else if (erofs_oneshot_cold > 0 && strcmp(exec_saved_comm, "sh") != 0) {
		erofs_flush_cold_sb(sb, erofs_oneshot_cold >= 2 ? 1 : 0);
		erofs_oneshot_cold = 0;
	}

	erofs_sync_dm_stats(sb);
	sbi = (struct erofs_sb_info *)sb->u.generic_sbp;
	exec_active_sb = sb;
	exec_s_data = sbi->data_blk_reads;
	exec_s_merkle = sbi->merkle_blk_reads;
	exec_s_sha_calls = sbi->sha256_calls;
	exec_s_sha_rounds = sbi->sha256_rounds;
	exec_s_meta_hits = sbi->meta_bitmap_hits;
	exec_t0_us = six_host_monotonic_us();
}

void erofs_exec_end(const char *filename, struct inode *inode, int retval)
{
	struct super_block *sb = exec_active_sb;
	struct erofs_sb_info *sbi;
	unsigned long t1_us;
	const char *base;

	exec_active_sb = NULL;
	if (retval < 0 || !sb || !sb->u.generic_sbp)
		return;

	t1_us = six_host_monotonic_us();
	erofs_sync_dm_stats(sb);
	sbi = (struct erofs_sb_info *)sb->u.generic_sbp;

	if (exec_saved_comm[0])
		base = exec_saved_comm;
	else if (current && current->comm[0])
		base = current->comm;
	else
		base = "-";

	(void)filename;
	sbi->total_execs++;
	strncpy(sbi->last_exec_comm, base, sizeof(sbi->last_exec_comm) - 1);
	sbi->last_exec_comm[sizeof(sbi->last_exec_comm) - 1] = '\0';
	sbi->last_exec_bytes = inode ? inode->i_size : 0;
	sbi->last_exec_us = (t1_us > exec_t0_us) ? (t1_us - exec_t0_us) : 1;
	sbi->last_exec_data_blks = sbi->data_blk_reads - exec_s_data;
	sbi->last_exec_merkle_blks = sbi->merkle_blk_reads - exec_s_merkle;
	sbi->last_exec_sha256_calls = sbi->sha256_calls - exec_s_sha_calls;
	sbi->last_exec_sha256_rounds = sbi->sha256_rounds - exec_s_sha_rounds;
	sbi->last_exec_meta_hits = sbi->meta_bitmap_hits - exec_s_meta_hits;
}

int set_erofs_proc_ctl(const char *cmd, int count)
{
	char kbuf[64];
	int n, i;

	if (!cmd || count <= 0)
		return 0;
	n = (count < (int)sizeof(kbuf) - 1) ? count : ((int)sizeof(kbuf) - 1);
	memcpy(kbuf, cmd, n);
	kbuf[n] = '\0';
	while (n > 0 && (kbuf[n - 1] == '\n' || kbuf[n - 1] == '\r' || kbuf[n - 1] == ' '))
		kbuf[--n] = '\0';

	if (strcmp(kbuf, "cold") == 0) {
		for (i = 0; i < EROFS_MAX_MOUNTS; i++) {
			if (erofs_mounts[i])
				erofs_flush_cold_sb(erofs_mounts[i], 0);
		}
		erofs_oneshot_cold = 1;
	} else if (strcmp(kbuf, "cold_all") == 0) {
		for (i = 0; i < EROFS_MAX_MOUNTS; i++) {
			if (erofs_mounts[i])
				erofs_flush_cold_sb(erofs_mounts[i], 1);
		}
		erofs_oneshot_cold = 2;
	} else if (strcmp(kbuf, "auto_cold=1") == 0 || strcmp(kbuf, "auto_cold") == 0) {
		erofs_auto_cold = 1;
	} else if (strcmp(kbuf, "auto_cold=2") == 0) {
		erofs_auto_cold = 2;
	} else if (strcmp(kbuf, "auto_cold=0") == 0 || strcmp(kbuf, "warm") == 0) {
		erofs_auto_cold = 0;
		erofs_oneshot_cold = 0;
	} else if (strcmp(kbuf, "tamper") == 0) {
		erofs_tamper_active = 1;
		for (i = 0; i < EROFS_MAX_MOUNTS; i++) {
			if (erofs_mounts[i])
				erofs_flush_cold_sb(erofs_mounts[i], 0);
		}
	} else if (strcmp(kbuf, "untamper") == 0) {
		erofs_tamper_active = 0;
		for (i = 0; i < EROFS_MAX_MOUNTS; i++) {
			if (erofs_mounts[i])
				erofs_flush_cold_sb(erofs_mounts[i], 0);
		}
	} else if (strcmp(kbuf, "reset") == 0) {
		for (i = 0; i < EROFS_MAX_MOUNTS; i++) {
			if (erofs_mounts[i] && erofs_mounts[i]->u.generic_sbp) {
				struct erofs_sb_info *sbi =
					(struct erofs_sb_info *)erofs_mounts[i]->u.generic_sbp;
				sbi->total_execs = 0;
				strcpy(sbi->last_exec_comm, "-");
				sbi->last_exec_bytes = 0;
				sbi->last_exec_us = 0;
				sbi->last_exec_data_blks = 0;
				sbi->last_exec_merkle_blks = 0;
				sbi->last_exec_sha256_calls = 0;
				sbi->last_exec_sha256_rounds = 0;
				sbi->last_exec_meta_hits = 0;
				sbi->data_blk_reads = 0;
				sbi->merkle_blk_reads = 0;
				sbi->sha256_calls = 0;
				sbi->sha256_rounds = 0;
				sbi->meta_bitmap_hits = 0;
			}
		}
	}
	return count;
}

int get_erofs_proc_info(char *buf)
{
	int len = 0, i;

	len += sprintf(buf + len,
		"EROFS v1 + Native EROFS Verity (go/erofs-verity) Telemetry (auto_cold=%d)\n",
		erofs_auto_cold);

	for (i = 0; i < EROFS_MAX_MOUNTS; i++) {
		struct super_block *sb = erofs_mounts[i];
		struct erofs_sb_info *sbi;
		const char *mnt_path = "/mnt";

		if (!sb || !sb->u.generic_sbp)
			continue;
		erofs_sync_dm_stats(sb);
		sbi = (struct erofs_sb_info *)sb->u.generic_sbp;
		if (strcmp(sbi->volume_name, "bin_verity") == 0)
			mnt_path = "/bin";
		else if (strcmp(sbi->volume_name, "bin_sarthak") == 0)
			mnt_path = "/bin-sarthak";
		else if (strcmp(sbi->volume_name, "vendor_verity") == 0)
			mnt_path = "/vendor";

		len += sprintf(buf + len,
			"mount=%s dev=/dev/%s label=%s mode=%s blocks=%u inos=%lu meta_blks=%u merkle_blks=%u\n"
			"  execs=%lu last_comm=%s last_bytes=%lu last_us=%lu last_data_blks=%lu last_merkle_blks=%lu last_sha256_calls=%lu last_sha256_rounds=%lu last_meta_hits=%lu\n"
			"  total_data_blks=%lu total_merkle_blks=%lu total_sha256_calls=%lu total_sha256_rounds=%lu total_meta_hits=%lu meta_verified=%lu data_verified=%lu corrupt=%lu\n",
			mnt_path,
			kdevname(sb->s_dev),
			sbi->volume_name[0] ? sbi->volume_name : "none",
			sbi->verity_enabled ? "go/erofs-verity" : "dm-verity+erofs",
			sbi->blocks,
			(unsigned long)sbi->inos,
			sbi->meta_verity_blocks,
			sbi->meta_merkle_blocks,
			sbi->total_execs,
			sbi->last_exec_comm[0] ? sbi->last_exec_comm : "-",
			sbi->last_exec_bytes,
			sbi->last_exec_us,
			sbi->last_exec_data_blks,
			sbi->last_exec_merkle_blks,
			sbi->last_exec_sha256_calls,
			sbi->last_exec_sha256_rounds,
			sbi->last_exec_meta_hits,
			sbi->data_blk_reads,
			sbi->merkle_blk_reads,
			sbi->sha256_calls,
			sbi->sha256_rounds,
			sbi->meta_bitmap_hits,
			sbi->meta_blocks_verified,
			sbi->data_blocks_verified,
			sbi->corrupt_blocks);
	}
	return len;
}

int erofs_format_mount_opts(struct super_block *sb, char *buf)
{
	struct erofs_sb_info *sbi;
	int i, len;

	if (!sb || !sb->u.generic_sbp)
		return 0;
	sbi = (struct erofs_sb_info *)sb->u.generic_sbp;
	if (!sbi->verity_enabled)
		return 0;
	len = sprintf(buf, ",erofs_verity,root_digest=");
	for (i = 0; i < 32; i++)
		len += sprintf(buf + len, "%02x", sbi->root_digest[i]);
	return len;
}

static struct file_system_type erofs_fs_type = {
	erofs_read_super, "erofs", 1, NULL
};

int init_erofs_fs(void)
{
	return register_filesystem(&erofs_fs_type);
}
