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
#include <asm/segment.h>
#include <asm/system.h>

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
};

static struct dm_device dm_devs[DM_MAX_DEVICES];
static int dm_sizes[64];
static int dm_blocksizes[64];

static inline unsigned int rotl32(unsigned int v, int c)
{
	return (v << c) | (v >> (32 - c));
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
	printk("device-mapper: v1.0 initialized (major %d, targets: linear, crypt, striped, zero, error)\n",
	       DM_MAJOR);
	return 0;
}
