/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		IP/TCP/UDP checksumming routines for SIX
 */

#include <net/checksum.h>
#include <linux/string.h>

unsigned int csum_partial(const unsigned char *buff, int len, unsigned int sum)
{
	unsigned int acc = sum;
	const unsigned short *p = (const unsigned short *)buff;
	while (len > 1) {
		acc += *p++;
		len -= 2;
	}
	if (len == 1) {
		acc += *(const unsigned char *)p;
	}
	while (acc >> 16) {
		acc = (acc & 0xffff) + (acc >> 16);
	}
	return acc;
}

unsigned int csum_partial_copy(const char *src, char *dst, int len, int sum)
{
	memcpy(dst, src, len);
	return csum_partial((const unsigned char *)dst, len, sum);
}

unsigned int csum_partial_copy_fromuser(const char *src, char *dst, int len, int sum)
{
	memcpy(dst, src, len);
	return csum_partial((const unsigned char *)dst, len, sum);
}
