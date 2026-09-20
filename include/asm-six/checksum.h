#ifndef _SIX_CHECKSUM_H
#define _SIX_CHECKSUM_H

#include <asm/byteorder.h>

unsigned int csum_partial(const unsigned char *buff, int len, unsigned int sum);
unsigned int csum_partial_copy(const char *src, char *dst, int len, int sum);
unsigned int csum_partial_copy_fromuser(const char *src, char *dst, int len, int sum);

static inline unsigned int csum_fold(unsigned int sum)
{
	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);
	return (~sum) & 0xffff;
}

static inline unsigned short ip_fast_csum(unsigned char *iph, unsigned int ihl)
{
	return csum_fold(csum_partial(iph, ihl << 2, 0));
}

static inline unsigned short int csum_tcpudp_magic(unsigned long saddr,
						   unsigned long daddr,
						   unsigned short len,
						   unsigned short proto,
						   unsigned int sum)
{
	unsigned long long s = (unsigned long long)sum;
	s += (saddr & 0xffff) + (saddr >> 16);
	s += (daddr & 0xffff) + (daddr >> 16);
	s += (unsigned short)htons(len);
	s += (unsigned short)(proto << 8);
	return csum_fold((unsigned int)s);
}

static inline unsigned short ip_compute_csum(unsigned char *buff, int len)
{
	return csum_fold(csum_partial(buff, len, 0));
}

#endif /* _SIX_CHECKSUM_H */
