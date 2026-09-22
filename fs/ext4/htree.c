/*
 * fs/ext4/htree.c
 *
 * Hashed B-Tree directory indexing (EXT4_INDEX_FL / dir_index) for SIX.
 * Implements upstream ext4fs_dirhash() (Half-MD4, TEA, and Legacy hashes)
 * and dx_probe() directory index block lookup.
 */

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/ext4_fs.h>
#include <linux/string.h>

#define F(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))
#define G(x, y, z) (((x) & (y)) + (((x) ^ (y)) & (z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))

#define ROUND(f, a, b, c, d, x, s) \
	(a += f(b, c, d) + x, a = (a << s) | (a >> (32 - s)))
#define K1 0
#define K2 013240474631UL
#define K3 015666365641UL

/*
 * Half-MD4 transform used by ext3/ext4 DX_HASH_HALF_MD4 (the default in mkfs.ext4)
 */
static void half_md4_transform(__u32 buf[4], __u32 const in[8])
{
	__u32 a = buf[0], b = buf[1], c = buf[2], d = buf[3];

	/* Round 1 */
	ROUND(F, a, b, c, d, in[0] + K1,  3);
	ROUND(F, d, a, b, c, in[1] + K1,  7);
	ROUND(F, c, d, a, b, in[2] + K1, 11);
	ROUND(F, b, c, d, a, in[3] + K1, 19);
	ROUND(F, a, b, c, d, in[4] + K1,  3);
	ROUND(F, d, a, b, c, in[5] + K1,  7);
	ROUND(F, c, d, a, b, in[6] + K1, 11);
	ROUND(F, b, c, d, a, in[7] + K1, 19);

	/* Round 2 */
	ROUND(G, a, b, c, d, in[1] + K2,  3);
	ROUND(G, d, a, b, c, in[3] + K2,  5);
	ROUND(G, c, d, a, b, in[5] + K2,  9);
	ROUND(G, b, c, d, a, in[7] + K2, 13);
	ROUND(G, a, b, c, d, in[0] + K2,  3);
	ROUND(G, d, a, b, c, in[2] + K2,  5);
	ROUND(G, c, d, a, b, in[4] + K2,  9);
	ROUND(G, b, c, d, a, in[6] + K2, 13);

	/* Round 3 */
	ROUND(H, a, b, c, d, in[3] + K3,  3);
	ROUND(H, d, a, b, c, in[7] + K3,  9);
	ROUND(H, c, d, a, b, in[2] + K3, 11);
	ROUND(H, b, c, d, a, in[6] + K3, 15);
	ROUND(H, a, b, c, d, in[1] + K3,  3);
	ROUND(H, d, a, b, c, in[5] + K3,  9);
	ROUND(H, c, d, a, b, in[0] + K3, 11);
	ROUND(H, b, c, d, a, in[4] + K3, 15);

	buf[0] += a;
	buf[1] += b;
	buf[2] += c;
	buf[3] += d;
}

static void str2hashbuf(const char *msg, int len, __u32 *buf, int num, int unsigned_flag)
{
	__u32 pad, val;
	int i;

	pad = (__u32)len | ((__u32)len << 8);
	pad |= pad << 16;

	val = pad;
	if (len > num * 4)
		len = num * 4;
	for (i = 0; i < len; i++) {
		if ((i % 4) == 0)
			val = pad;
		if (unsigned_flag)
			val = ((__u32)(unsigned char)msg[i]) + (val << 8);
		else
			val = ((__u32)(signed char)msg[i]) + (val << 8);
		if ((i % 4) == 3) {
			*buf++ = val;
			val = pad;
			num--;
		}
	}
	if (--num >= 0)
		*buf++ = val;
	while (--num >= 0)
		*buf++ = pad;
}

__u32 ext4fs_dirhash(const char *name, int len, int hash_version, const __u32 *seed)
{
	__u32 buf[4];
	__u32 in[8];
	const char *p = name;
	int rem = len;
	int unsigned_chars = 0;

	buf[0] = 0x67452301;
	buf[1] = 0xefcdab89;
	buf[2] = 0x98badcfe;
	buf[3] = 0x10325476;

	if (seed && (seed[0] || seed[1] || seed[2] || seed[3]))
		memcpy(buf, seed, sizeof(buf));

	if (hash_version >= DX_HASH_LEGACY_UNSIGNED) {
		unsigned_chars = 1;
		hash_version -= 3;
	}

	if (hash_version == DX_HASH_HALF_MD4 || hash_version == DX_HASH_TEA) {
		while (rem > 0) {
			str2hashbuf(p, rem, in, 8, unsigned_chars);
			half_md4_transform(buf, in);
			rem -= 32;
			p += 32;
		}
		return buf[1] & ~1U;
	}

	/* Fallback legacy hash */
	{
		__u32 h = 0;
		int i;
		for (i = 0; i < len; i++)
			h = (h * 33) + (unsigned char)name[i];
		return h & ~1U;
	}
}

/*
 * Probe an HTree-indexed directory (EXT4_INDEX_FL) for `name` (length `namelen`).
 * Reads block 0 (dx_root), computes `ext4fs_dirhash()`, binary-searches the
 * dx_entry table to identify the target leaf directory block, and scans it.
 * Falls back cleanly if multiple hash collisions span into the next leaf block.
 */
struct buffer_head *ext4_dx_find_entry(struct inode *dir, const char *name,
				       int namelen, struct ext2_dir_entry **res_dir)
{
	struct super_block *sb = dir->i_sb;
	struct buffer_head *bh0;
	int err = 0;
	unsigned char hash_version;
	struct dx_countlimit *cl;
	struct dx_entry *entries;
	int count, i;
	__u32 hash;
	__u32 target_block = 0;

	bh0 = ext4_bread(dir, 0, 0, &err);
	if (!bh0)
		return NULL;

	/*
	 * In an ext4 dx_root block (block 0 of an indexed directory):
	 *   offset 0..11:  fake "." dirent (rec_len = 12)
	 *   offset 12..23: fake ".." dirent (rec_len = blocksize - 12)
	 *   offset 24..31: dx_root_info (reserved_zero[4], hash_version[1],
	 *                                info_length[1]=8, indirect_levels[1], unused[1])
	 *   offset 32..35: dx_countlimit (limit, count)
	 *   offset 36..39: block_0 (logical block number of first leaf)
	 *   offset 40+:    dx_entry[1..count-1] (hash, block)
	 */
	hash_version = (unsigned char)bh0->b_data[28];
	cl = (struct dx_countlimit *)(bh0->b_data + 32);
	entries = (struct dx_entry *)(bh0->b_data + 32);
	count = cl->count;

	if (count < 1 || count > (int)((sb->s_blocksize - 32) / sizeof(struct dx_entry))) {
		brelse(bh0);
		return NULL;
	}

	hash = ext4fs_dirhash(name, namelen, hash_version, NULL);
	target_block = entries[0].block & 0x0fffffff;
	for (i = 1; i < count; i++) {
		if (hash < entries[i].hash)
			break;
		target_block = entries[i].block & 0x0fffffff;
	}
	brelse(bh0);

	/* Scan the target leaf directory block identified by the HTree index */
	if (target_block > 0) {
		struct buffer_head *leaf_bh = ext4_bread(dir, (int)target_block, 0, &err);
		if (leaf_bh) {
			unsigned long offset = 0;
			while (offset < sb->s_blocksize) {
				struct ext2_dir_entry *de =
					(struct ext2_dir_entry *)(leaf_bh->b_data + offset);
				int dlen = EXT4_DIR_NAMELEN(de);
				if (de->rec_len < EXT2_DIR_REC_LEN(1))
					break;
				if (de->inode != 0 && dlen == namelen &&
				    memcmp(name, de->name, namelen) == 0) {
					*res_dir = de;
					return leaf_bh;
				}
				offset += de->rec_len;
			}
			brelse(leaf_bh);
		}
	}
	return NULL;
}
