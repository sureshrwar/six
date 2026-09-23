/*
 * /etc/demos/dm_demo.c - SIX Device Mapper (linear vs crypt) demonstration
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
	printf("  %-26s: ", label);
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
	const char *secret = "SIX-DM-VAULT: Account #42910 Balance $9,876,543.21";
	unsigned char buf[512];
	int fd;

	printf("=== SIX Device Mapper (linear vs crypt) Demo ===\n\n");

	/* 1. Write secret string to /dev/mapper/linear_vol (sector 0 of /dev/hdc) */
	fd = open("/dev/mapper/linear_vol", O_RDWR);
	if (fd >= 0) {
		memset(buf, 0, sizeof(buf));
		strcpy((char *)buf, secret);
		write(fd, buf, sizeof(buf));
		close(fd);
	}

	/* 2. Write the exact same secret string to /dev/mapper/crypt_vol (sector 40960 of /dev/hdc) */
	fd = open("/dev/mapper/crypt_vol", O_RDWR);
	if (fd >= 0) {
		memset(buf, 0, sizeof(buf));
		strcpy((char *)buf, secret);
		write(fd, buf, sizeof(buf));
		close(fd);
	}

	/* 3. Read back through the mapped devices */
	printf("1. Reading through Device Mapper virtual block devices:\n");
	fd = open("/dev/mapper/linear_vol", O_RDONLY);
	if (fd >= 0) {
		memset(buf, 0, sizeof(buf));
		read(fd, buf, sizeof(buf));
		close(fd);
		printf("  /dev/mapper/linear_vol -> \"%s\"\n", (char *)buf);
	}
	fd = open("/dev/mapper/crypt_vol", O_RDONLY);
	if (fd >= 0) {
		memset(buf, 0, sizeof(buf));
		read(fd, buf, sizeof(buf));
		close(fd);
		printf("  /dev/mapper/crypt_vol  -> \"%s\"\n\n", (char *)buf);
	}

	/* 4. Inspect raw physical sectors on /dev/hdc! */
	printf("2. Inspecting raw physical sectors directly on /dev/hdc:\n");
	fd = open("/dev/hdc", O_RDONLY);
	if (fd >= 0) {
		memset(buf, 0, sizeof(buf));
		lseek(fd, 0L, 0);
		read(fd, buf, 512);
		print_hex_ascii("/dev/hdc @ sec 0 (linear)", buf, 16);

		memset(buf, 0, sizeof(buf));
		lseek(fd, 40960L * 512L, 0);
		read(fd, buf, 512);
		print_hex_ascii("/dev/hdc @ sec 40960 (crypt)", buf, 16);
		close(fd);
	}

	return 0;
}
