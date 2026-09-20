#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/types.h>
#include <linux/time.h>

extern int close(int fd);
extern int socket(int domain, int type, int protocol);
extern int sendto(int sockfd, const void *buf, int len, int flags, const struct sockaddr *dest_addr, int addrlen);
extern int recv(int sockfd, void *buf, int len, int flags);
extern int select(int n, fd_set *inp, fd_set *outp, fd_set *exp, struct timeval *tvp);

int inet_aton(const char *cp, struct in_addr *inp)
{
	unsigned long val = 0;
	int base = 10;
	char c;
	unsigned int parts[4];
	unsigned int *pp = parts;
	int nparts = 0;

	for (;;) {
		val = 0;
		while ((c = *cp) != '\0') {
			if (c >= '0' && c <= '9') {
				val = (val * base) + (c - '0');
				cp++;
			} else {
				break;
			}
		}
		if (*cp == '.') {
			if (pp >= parts + 3)
				return 0;
			*pp++ = val;
			cp++;
		} else {
			break;
		}
	}
	if (*cp != '\0')
		return 0;
	*pp++ = val;
	nparts = pp - parts;

	switch (nparts) {
	case 1:
		val = parts[0];
		break;
	case 2:
		val = (parts[0] << 24) | (parts[1] & 0xffffff);
		break;
	case 3:
		val = (parts[0] << 24) | ((parts[1] & 0xff) << 16) | (parts[2] & 0xffff);
		break;
	case 4:
		val = (parts[0] << 24) | ((parts[1] & 0xff) << 16) | ((parts[2] & 0xff) << 8) | (parts[3] & 0xff);
		break;
	default:
		return 0;
	}
	if (inp)
		inp->s_addr = htonl(val);
	return 1;
}

in_addr_t inet_addr(const char *cp)
{
	struct in_addr val;
	if (inet_aton(cp, &val))
		return val.s_addr;
	return INADDR_NONE;
}

char *inet_ntoa(struct in_addr in)
{
	static char b[18];
	register char *p;
	p = (char *)&in;
#define UC(b)   (((int)b)&0xff)
	sprintf(b, "%d.%d.%d.%d", UC(p[0]), UC(p[1]), UC(p[2]), UC(p[3]));
	return b;
}

/* Minimal DNS Query via 10.0.2.2:53 */
static int dns_query(const char *name, struct in_addr *out_ip)
{
	int s;
	struct sockaddr_in saddr;
	static unsigned char packet[512];
	int pos = 12;
	const char *src = name;
	struct timeval tv;
	fd_set rfds;
	int r, ancount, idx;

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0)
		return 0;

	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(53);
	inet_aton("10.0.2.2", &saddr.sin_addr);

	memset(packet, 0, sizeof(packet));
	packet[0] = 0x12; packet[1] = 0x34; /* ID */
	packet[2] = 0x01; packet[3] = 0x00; /* Standard query with recursion */
	packet[4] = 0x00; packet[5] = 0x01; /* 1 question */

	while (*src) {
		const char *dot = strchr(src, '.');
		int len = dot ? (dot - src) : strlen(src);
		if (len > 63 || pos + len + 1 >= sizeof(packet) - 10) {
			close(s);
			return 0;
		}
		packet[pos++] = (unsigned char)len;
		memcpy(packet + pos, src, len);
		pos += len;
		if (!dot) break;
		src = dot + 1;
	}
	packet[pos++] = 0; /* root */

	/* QTYPE = A (1), QCLASS = IN (1) */
	packet[pos++] = 0; packet[pos++] = 1;
	packet[pos++] = 0; packet[pos++] = 1;

	if (sendto(s, packet, pos, 0, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
		close(s);
		return 0;
	}

	tv.tv_sec = 3;
	tv.tv_usec = 0;
	FD_ZERO(&rfds);
	FD_SET(s, &rfds);

	int sret = select(s + 1, &rfds, NULL, NULL, &tv);
	if (sret <= 0) {
		close(s);
		return 0;
	}

	r = recv(s, packet, sizeof(packet), 0);
	close(s);
	if (r < 12)
		return 0;

	ancount = (packet[6] << 8) | packet[7];
	if (ancount <= 0)
		return 0;

	/* Skip question section */
	idx = 12;
	while (idx < r && packet[idx] != 0) {
		if ((packet[idx] & 0xc0) == 0xc0) {
			idx += 2;
			goto qdone;
		}
		idx += packet[idx] + 1;
	}
	if (idx < r && packet[idx] == 0)
		idx++;
qdone:
	idx += 4; /* skip QTYPE and QCLASS */

	/* Parse Answers */
	while (idx < r && ancount-- > 0) {
		int type, rdlen;
		if ((packet[idx] & 0xc0) == 0xc0) {
			idx += 2;
		} else {
			while (idx < r && packet[idx] != 0) {
				idx += packet[idx] + 1;
			}
			if (idx < r) idx++;
		}
		if (idx + 10 > r)
			break;

		type = (packet[idx] << 8) | packet[idx + 1];
		rdlen = (packet[idx + 8] << 8) | packet[idx + 9];
		idx += 10;

		if (type == 1 && rdlen == 4 && idx + 4 <= r) {
			memcpy(&out_ip->s_addr, packet + idx, 4);
			return 1;
		}
		idx += rdlen;
	}
	return 0;
}

/* gethostbyname */
static struct hostent static_host;
static char *static_aliases[1] = { NULL };
static unsigned long static_addr;
static char *static_addr_list[2] = { (char *)&static_addr, NULL };
static char static_name[64];

struct hostent *gethostbyname(const char *name)
{
	struct in_addr in;
	int found = 0;

	if (!name)
		return NULL;

	if (strcmp(name, "localhost") == 0) {
		static_addr = htonl(0x7f000001);
		strncpy(static_name, "localhost", sizeof(static_name) - 1);
		static_name[sizeof(static_name) - 1] = '\0';
		found = 1;
	} else if (inet_aton(name, &in)) {
		static_addr = in.s_addr;
		strncpy(static_name, name, sizeof(static_name) - 1);
		static_name[sizeof(static_name) - 1] = '\0';
		found = 1;
	} else {
		/* Try to parse /etc/hosts */
		FILE *f = fopen("/etc/hosts", "r");
		if (f) {
			char line[128];
			while (fgets(line, sizeof(line), f)) {
				char *p = line;
				char *ip_str, *host_str;
				while (*p == ' ' || *p == '\t') p++;
				if (*p == '#' || *p == '\n' || *p == '\0') continue;
				ip_str = p;
				while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
				if (*p) *p++ = '\0';
				while (*p == ' ' || *p == '\t') p++;
				host_str = p;
				while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
				if (*p) *p = '\0';
				if (strcmp(host_str, name) == 0 && inet_aton(ip_str, &in)) {
					static_addr = in.s_addr;
					strncpy(static_name, name, sizeof(static_name) - 1);
					static_name[sizeof(static_name) - 1] = '\0';
					found = 1;
					break;
				}
			}
			fclose(f);
		}

		/* Fallback to dynamic DNS query */
		if (!found) {
			if (dns_query(name, &in)) {
				static_addr = in.s_addr;
				strncpy(static_name, name, sizeof(static_name) - 1);
				static_name[sizeof(static_name) - 1] = '\0';
				found = 1;
			}
		}
	}

	if (!found)
		return NULL;

	static_host.h_name = static_name;
	static_host.h_aliases = static_aliases;
	static_host.h_addrtype = AF_INET;
	static_host.h_length = sizeof(static_addr);
	static_host.h_addr_list = static_addr_list;
	return &static_host;
}
