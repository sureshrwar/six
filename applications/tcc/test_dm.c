/*
 * /etc/demos/dm_demo.c - SIX Device Mapper (linear0 vs crypt0) live inspector
 *
 * Compiles inside the guest with:
 *   tcc -o /tmp/dm_demo /etc/demos/dm_demo.c
 *   /tmp/dm_demo
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

static void print_hex_ascii(const char *label, const unsigned char *buf, int len)
{
	int i;
	printf("  %-32s: ", label);
	for (i = 0; i < len; i++)
		printf("%02x ", buf[i]);
	printf(" |");
	for (i = 0; i < len; i++) {
		unsigned char c = buf[i];
		printf("%c", (c >= 32 && c < 127) ? c : '.');
	}
	printf("|\n");
}

int main(void)
{
	unsigned char buf[512];
	int fd;

	printf("=== SIX Device Mapper (linear0 vs crypt0) Live Inspector ===\n\n");

	/* 1. Compare ext2 superblock magic & volume label at offset 1024 + 0x38 (sector 2) */
	printf("1. Inspecting ext2 Superblock (sector 2, offset +0x38..+0x87):\n");
	fd = open("/dev/mapper/linear0", O_RDONLY);
	if (fd >= 0) {
		lseek(fd, 1024L, 0);
		read(fd, buf, 512);
		close(fd);
		printf("  /dev/mapper/linear0 magic=0x%04x, label=\"%s\"\n",
		       *(unsigned short *)(buf + 0x38), (char *)(buf + 0x78));
	}
	fd = open("/dev/mapper/crypt0", O_RDONLY);
	if (fd >= 0) {
		lseek(fd, 1024L, 0);
		read(fd, buf, 512);
		close(fd);
		printf("  /dev/mapper/crypt0  magic=0x%04x, label=\"%s\"\n\n",
		       *(unsigned short *)(buf + 0x38), (char *)(buf + 0x78));
	}

	/* 2. Inspect raw /dev/hdc backing sectors for both volumes */
	printf("2. Inspecting raw physical sectors on /dev/hdc (Superblock volume name @ +0x78):\n");
	fd = open("/dev/hdc", O_RDONLY);
	if (fd >= 0) {
		lseek(fd, 2L * 512L, 0);
		read(fd, buf, 512);
		print_hex_ascii("/dev/hdc @ sec 2 (linear0)", buf + 0x78, 12);

		lseek(fd, 40962L * 512L, 0);
		read(fd, buf, 512);
		print_hex_ascii("/dev/hdc @ sec 40962 (crypt0)", buf + 0x78, 12);
		close(fd);
	}

	return 0;
}
