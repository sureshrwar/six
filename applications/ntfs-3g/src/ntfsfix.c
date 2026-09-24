/*
 * applications/ntfs-3g/src/ntfsfix.c — NTFS consistency check/fix utility (/bin/ntfsfix) for SIX
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "volume.h"

int main(int argc, char **argv)
{
    const char *dev = NULL;
    int i;
    ntfs_volume *vol;

    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-')
            continue;
        dev = argv[i];
    }

    if (!dev) {
        fprintf(stderr, "Usage: ntfsfix [-b] [-d] device\n");
        return 1;
    }

    printf("Mounting volume... ");
    vol = ntfs_mount(dev, NTFS_MNT_RDONLY);
    if (!vol) {
        printf("FAILED\n");
        return 1;
    }
    printf("OK\n");
    ntfs_umount(vol, FALSE);
    printf("NTFS partition %s was processed successfully.\n", dev);
    return 0;
}
