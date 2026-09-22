/*
 * fs/ext4/ioctl.c
 *
 * Ioctl support for SIX's fs/ext4 driver, including EXT4_IOC_GET_INFO
 * for /bin/ext4info live extent-tree and inode inspection.
 */

#include <asm/segment.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ext4_fs.h>
#include <linux/ioctl.h>
#include <linux/sched.h>
#include <linux/mm.h>

int ext4_ioctl(struct inode *inode, struct file *filp, unsigned int cmd,
	       unsigned long arg)
{
	unsigned int flags;

	switch (cmd) {
	case EXT4_IOC_GET_INFO: {
		struct ext4_inode_inspect info;
		int err = verify_area(VERIFY_WRITE, (void *)arg, sizeof(info));
		if (err)
			return err;
		ext4_ext_inspect(inode, &info);
		memcpy_tofs((void *)arg, &info, sizeof(info));
		return 0;
	}
	case EXT2_IOC_GETFLAGS:
		flags = inode->u.ext2_i.i_flags;
		put_user(flags, (int *)arg);
		return 0;
	case EXT2_IOC_SETFLAGS: {
		if ((current->fsuid != inode->i_uid) && !suser())
			return -EPERM;
		if (IS_RDONLY(inode))
			return -EROFS;
		flags = get_user((int *)arg);
		inode->u.ext2_i.i_flags = flags;
		inode->i_ctime = CURRENT_TIME;
		inode->i_dirt = 1;
		return 0;
	}
	case EXT2_IOC_GETVERSION:
		put_user(inode->u.ext2_i.i_version, (int *)arg);
		return 0;
	default:
		return -EINVAL;
	}
}
