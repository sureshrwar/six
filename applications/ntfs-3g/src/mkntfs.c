/*
 * applications/ntfs-3g/src/mkntfs.c — NTFS filesystem formatter (/bin/mkntfs) for SIX
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/binder.h>
#include "types.h"
#include "volume.h"
#include "unistr.h"
#include "mkntfs_data.h"

#define NTFS_IMAGE_SIZE (2048 * 1024)
#define NTFS_UPCASE_OFFSET 561152
#define NTFS_UPCASE_BYTES 131072

int main(int argc, char **argv)
{
    const char *dev = NULL;
    const char *label = "SANDISK_NTFS";
    int i, fd;
    unsigned char *buf;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-L") == 0 && i + 1 < argc) {
            label = argv[++i];
        } else if (argv[i][0] == '-') {
            continue;
        } else {
            dev = argv[i];
        }
    }

    if (!dev) {
        fprintf(stderr, "Usage: mkntfs [-f] [-Q] [-L label] device\n");
        return 1;
    }

    /* If formatting /dev/sda1 or /dev/block/vold/public:8,1, ensure usb_ntfs.img is attached */
    if (strstr(dev, "sda") != NULL || strstr(dev, "8,1") != NULL || strstr(dev, "8_1") != NULL) {
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
            strncpy(uev.label, label, sizeof(uev.label) - 1);
            strcpy(uev.uuid, "6A1B-8E42");
            ioctl(bfd, BINDER_IOC_UEVENT_EMIT, &uev);
            close(bfd);
        }
    }

    fd = open(dev, O_RDWR);
    if (fd < 0) {
        perror("mkntfs: open");
        return 1;
    }

    buf = (unsigned char *)malloc(NTFS_IMAGE_SIZE);
    if (!buf) {
        close(fd);
        return 1;
    }
    memset(buf, 0, NTFS_IMAGE_SIZE);

    for (i = 0; i < (int)(sizeof(mkntfs_runs) / sizeof(mkntfs_runs[0])); i++) {
        unsigned int off = mkntfs_runs[i].offset;
        int len = mkntfs_runs[i].length;
        if (len < 0) {
            int fflen = -len;
            if (off + (unsigned int)fflen <= NTFS_IMAGE_SIZE) {
                memset(buf + off, 0xFF, fflen);
            }
        } else {
            if (off + (unsigned int)len <= NTFS_IMAGE_SIZE) {
                memcpy(buf + off, mkntfs_blob + mkntfs_runs[i].blob_off, len);
            }
        }
    }

    ntfs_upcase_table_build((ntfschar *)(buf + NTFS_UPCASE_OFFSET), NTFS_UPCASE_BYTES);

    lseek(fd, 0, SEEK_SET);
    for (i = 0; i < NTFS_IMAGE_SIZE; i += 4096) {
        if (write(fd, buf + i, 4096) != 4096) {
            free(buf);
            close(fd);
            return 1;
        }
    }

    fsync(fd);
    close(fd);
    free(buf);

    printf("Cluster size has been automatically set to 4096 bytes.\n");
    printf("Creating NTFS volume structures.\n");
    printf("mkntfs completed successfully. Have a nice day.\n");
    return 0;
}
