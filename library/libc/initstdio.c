#include <stdio.h>

struct iobuf __stdin = {
        0, 0, _IOREAD, 0,
        (unsigned char *)NULL, (unsigned char *)NULL,
};

struct iobuf __stdout = {
        0, 1, _IOWRITE, 0,
        (unsigned char *)NULL, (unsigned char *)NULL,
};

struct iobuf __stderr = {
        0, 2, _IOWRITE | _IOLBF, 0,
        (unsigned char *)NULL, (unsigned char *)NULL,
};

FILE *iotab[FOPEN_MAX] = {
        &__stdin,
        &__stdout,
        &__stderr,
        0
};

