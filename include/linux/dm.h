#ifndef _LINUX_DM_H
#define _LINUX_DM_H

#ifndef DM_MAJOR
#define DM_MAJOR		62
#endif

#define DM_MAX_DEVICES		8
#define DM_CONTROL_MINOR	63
#define DM_MAX_TARGETS		4
#define DM_NAME_LEN		32
#define DM_KEY_LEN		64
#define DM_CIPHER_LEN		16

#define DM_TARGET_NONE		0
#define DM_TARGET_LINEAR	1
#define DM_TARGET_CRYPT		2
#define DM_TARGET_STRIPED	3
#define DM_TARGET_ZERO		4
#define DM_TARGET_ERROR		5

#define DM_IOC_CREATE		0x4401
#define DM_IOC_REMOVE		0x4402
#define DM_IOC_SUSPEND		0x4403
#define DM_IOC_RESUME		0x4404
#define DM_IOC_STATUS		0x4405
#define DM_IOC_REMOVE_ALL	0x4406

struct dm_target_spec {
	unsigned long start_sector;
	unsigned long num_sectors;
	int type;
	unsigned short bdev;
	unsigned long offset_sector;
	unsigned short bdev2;
	unsigned long offset_sector2;
	unsigned long chunk_sectors;
	unsigned long iv_offset;
	char cipher[DM_CIPHER_LEN];
	char key[DM_KEY_LEN];
	char dev_name[32];
	char dev_name2[32];
};

struct dm_ioctl_req {
	int minor;
	char name[DM_NAME_LEN];
	int active;
	int suspended;
	int ro;
	unsigned long total_sectors;
	unsigned long read_ios;
	unsigned long write_ios;
	unsigned long read_sectors;
	unsigned long write_sectors;
	int num_targets;
	struct dm_target_spec targets[DM_MAX_TARGETS];
};

#ifdef __KERNEL__
int dm_init(void);
int get_dm_status_proc(char *buf);
#endif

#endif /* _LINUX_DM_H */
