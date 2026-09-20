#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>

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

/* Minimal gethostbyname */
static struct hostent static_host;
static char *static_aliases[1] = { NULL };
static unsigned long static_addr;
static char *static_addr_list[2] = { (char *)&static_addr, NULL };
static char static_name[64];

struct hostent *gethostbyname(const char *name)
{
	struct in_addr in;
	if (!name)
		return NULL;

	if (strcmp(name, "localhost") == 0) {
		static_addr = htonl(0x7f000001);
		strcpy(static_name, "localhost");
	} else if (inet_aton(name, &in)) {
		static_addr = in.s_addr;
		strncpy(static_name, name, sizeof(static_name) - 1);
		static_name[sizeof(static_name) - 1] = '\0';
	} else {
		/* Try to parse /etc/hosts */
		FILE *f = fopen("/etc/hosts", "r");
		int found = 0;
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
		if (!found)
			return NULL;
	}

	static_host.h_name = static_name;
	static_host.h_aliases = static_aliases;
	static_host.h_addrtype = AF_INET;
	static_host.h_length = sizeof(static_addr);
	static_host.h_addr_list = static_addr_list;
	return &static_host;
}
