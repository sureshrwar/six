#ifndef _SYS_MOUNT_H
#define _SYS_MOUNT_H

/*
 * Guest-side declarations for mount(2) and umount(2).
 *
 * The stubs in library/sys/mount.c and library/sys/umount.c have existed
 * since the 2005 import, but nothing in the guest ever called them, so
 * there was never a header to declare them.  The values below are copied
 * from the kernel's <linux/fs.h>; they are part of the system call
 * interface, so they have to agree exactly.
 */

#define MS_RDONLY	1	/* Mount read-only */
#define MS_NOSUID	2	/* Ignore suid and sgid bits */
#define MS_NODEV	4	/* Disallow access to device special files */
#define MS_NOEXEC	8	/* Disallow program execution */
#define MS_SYNCHRONOUS	16	/* Writes are synced at once */
#define MS_REMOUNT	32	/* Alter flags of a mounted FS */

/*
 * The magic number has to be or-ed into the flags word.  Without it
 * do_mount() treats the call as the pre-0.99 two-argument form and ignores
 * both the flags and the filesystem-specific `data' argument entirely --
 * silently, so a mount asked for read-only would come up read-write.
 */
#define MS_MGC_VAL	0xC0ED0000
#define MS_MGC_MSK	0xffff0000

/*
 * Both return 0, or -1 with errno set, like every other wrapper in
 * library/sys/.
 */
extern int mount(char *dev, char *dir, char *type,
		 unsigned long new_flags, void *data);
extern int umount(char *name);

#endif /* _SYS_MOUNT_H */
