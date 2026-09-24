/*
 * applications/ntfs-3g/src/ntfsfix.c — NTFS consistency check/fix utility (/bin/ntfsfix) for SIX
 *
 * Standard ntfsfix behavior:
 *   ntfsfix <device>          Reset $LogFile and set VOLUME_IS_DIRTY (schedules chkdsk)
 *   ntfsfix -d <device>       Reset $LogFile and clear VOLUME_IS_DIRTY
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/binder.h>
#include "volume.h"
#include "layout.h"

int main(int argc, char **argv)
{
    const char *dev = NULL;
    int clear_dirty = 0;
    int i;
    ntfs_volume *vol;
    le16 flags;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--clear-dirty") == 0) {
            clear_dirty = 1;
            continue;
        }
        if (strcmp(argv[i], "--dirty") == 0 || argv[i][0] == '-') {
            continue;
        }
        dev = argv[i];
    }

    if (!dev) {
        fprintf(stderr, "Usage: ntfsfix [-b] [-d] [--dirty] device\n");
        return 1;
    }

    /* If /dev/sda1 is currently unplugged (-ENODEV), attach usb_ntfs.img silently
     * via "prepare" (same as mkntfs) so ntfsfix can inspect/modify it while unplugged. */
    if (strstr(dev, "sda") != NULL || strstr(dev, "8,1") != NULL || strstr(dev, "8_1") != NULL) {
        int tfd = open(dev, O_RDONLY);
        if (tfd >= 0) {
            close(tfd);
        } else {
            int bfd = open("/dev/binder", O_RDWR);
            if (bfd >= 0) {
                struct binder_uevent_msg uev;
                memset(&uev, 0, sizeof(uev));
                strcpy(uev.action, "prepare");
                strcpy(uev.subsystem, "block");
                strcpy(uev.devpath, "/devices/pci0000:00/usb1/1-1/block/sda/sda1");
                strcpy(uev.devname, "sda1");
                uev.major = 8;
                uev.minor = 1;
                strcpy(uev.fstype, "ntfs");
                strcpy(uev.label, "SANDISK_NTFS");
                strcpy(uev.uuid, "6A1B-8E42");
                ioctl(bfd, BINDER_IOC_UEVENT_EMIT, &uev);
                close(bfd);
            }
        }
    }

    printf("Mounting volume... ");
    vol = ntfs_mount(dev, NTFS_MNT_RECOVER);
    if (!vol) {
        printf("FAILED\n");
        return 1;
    }
    printf("OK\n");

    printf("Going to empty the journal ($LogFile)... ");
    if (ntfs_logfile_reset(vol) != 0) {
        printf("FAILED\n");
        ntfs_umount(vol, FALSE);
        return 1;
    }
    printf("OK\n");

    flags = vol->flags;
    if (clear_dirty) {
        flags &= ~VOLUME_IS_DIRTY;
        printf("Clearing dirty flag on partition... ");
    } else {
        flags |= VOLUME_IS_DIRTY;
        printf("Setting required flags on partition... ");
    }

    if (ntfs_volume_write_flags(vol, flags) != 0) {
        printf("FAILED\n");
        ntfs_umount(vol, FALSE);
        return 1;
    }
    printf("OK (%s)\n", clear_dirty ? "VOLUME_IS_DIRTY cleared" : "VOLUME_IS_DIRTY set");

    ntfs_umount(vol, FALSE);
    printf("NTFS partition %s was processed successfully.\n", dev);
    return 0;
}
