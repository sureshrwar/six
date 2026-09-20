#ifndef _SYS_MMAN_H
#define _SYS_MMAN_H

#include <linux/mman.h>
#include <linux/types.h>

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int munmap(void *addr, size_t length);
int mprotect(void *addr, size_t len, int prot);

#endif
