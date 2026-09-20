/*
 * ifconfig - Network interface configuration utility for SIX
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

extern int close(int fd);
extern int ioctl(int fd, int request, ...);

#define SIOCGIFCONF	0x8912
#define SIOCGIFFLAGS	0x8913
#define SIOCSIFFLAGS	0x8914
#define SIOCGIFADDR	0x8915
#define SIOCSIFADDR	0x8916
#define SIOCGIFBRDADDR	0x8919
#define SIOCSIFBRDADDR	0x891a
#define SIOCGIFNETMASK	0x891b
#define SIOCSIFNETMASK	0x891c
#define SIOCGIFMTU	0x8921
#define SIOCGIFHWADDR	0x8927

#define IFF_UP		0x1
#define IFF_BROADCAST	0x2
#define IFF_DEBUG	0x4
#define IFF_LOOPBACK	0x8
#define IFF_POINTOPOINT	0x10
#define IFF_NOTRAILERS	0x20
#define IFF_RUNNING	0x40
#define IFF_NOARP	0x80
#define IFF_PROMISC	0x100
#define IFF_ALLMULTI	0x200
#define IFF_MULTICAST	0x1000

struct ifreq {
	char ifr_name[16];
	union {
		struct sockaddr ifru_addr;
		struct sockaddr ifru_dstaddr;
		struct sockaddr ifru_broadaddr;
		struct sockaddr ifru_netmask;
		struct sockaddr ifru_hwaddr;
		short ifru_flags;
		int ifru_metric;
		int ifru_mtu;
		char ifru_data[16];
	} ifr_ifru;
};

#define ifr_addr      ifr_ifru.ifru_addr
#define ifr_dstaddr   ifr_ifru.ifru_dstaddr
#define ifr_broadaddr ifr_ifru.ifru_broadaddr
#define ifr_netmask   ifr_ifru.ifru_netmask
#define ifr_hwaddr    ifr_ifru.ifru_hwaddr
#define ifr_flags     ifr_ifru.ifru_flags
#define ifr_mtu       ifr_ifru.ifru_mtu

struct ifconf {
	int ifc_len;
	union {
		char *ifcu_buf;
		struct ifreq *ifcu_req;
	} ifc_ifcu;
};
#define ifc_buf ifc_ifcu.ifcu_buf
#define ifc_req ifc_ifcu.ifcu_req

static void display_if(int sfd, const char *name)
{
	struct ifreq ifr;
	short flags = 0;
	int mtu = 0;
	char ip_str[32] = "none";
	char bcast_str[32] = "none";
	char mask_str[32] = "none";
	unsigned char hw[6] = {0};
	int has_hw = 0;

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, name, sizeof(ifr.ifr_name) - 1);

	if (ioctl(sfd, SIOCGIFFLAGS, &ifr) < 0) {
		printf("%s: error fetching interface flags\n", name);
		return;
	}
	flags = ifr.ifr_flags;

	if (ioctl(sfd, SIOCGIFADDR, &ifr) == 0) {
		struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;
		strcpy(ip_str, inet_ntoa(sin->sin_addr));
	}

	if (ioctl(sfd, SIOCGIFBRDADDR, &ifr) == 0) {
		struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_broadaddr;
		strcpy(bcast_str, inet_ntoa(sin->sin_addr));
	}

	if (ioctl(sfd, SIOCGIFNETMASK, &ifr) == 0) {
		struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_netmask;
		strcpy(mask_str, inet_ntoa(sin->sin_addr));
	}

	if (ioctl(sfd, SIOCGIFMTU, &ifr) == 0) {
		mtu = ifr.ifr_mtu;
	}

	if (ioctl(sfd, SIOCGIFHWADDR, &ifr) == 0) {
		memcpy(hw, ifr.ifr_hwaddr.sa_data, 6);
		has_hw = 1;
	}

	if (flags & IFF_LOOPBACK) {
		printf("%-9s Link encap:Local Loopback\n", name);
	} else {
		printf("%-9s Link encap:Ethernet", name);
		if (has_hw) {
			printf("  HWaddr %02X:%02X:%02X:%02X:%02X:%02X",
			       hw[0], hw[1], hw[2], hw[3], hw[4], hw[5]);
		}
		printf("\n");
	}

	printf("          inet addr:%s  Bcast:%s  Mask:%s\n",
	       ip_str, bcast_str, mask_str);

	printf("          ");
	if (flags & IFF_UP) printf("UP ");
	if (flags & IFF_BROADCAST) printf("BROADCAST ");
	if (flags & IFF_LOOPBACK) printf("LOOPBACK ");
	if (flags & IFF_RUNNING) printf("RUNNING ");
	if (flags & IFF_MULTICAST) printf("MULTICAST ");
	printf(" MTU:%d\n\n", mtu);
}

int main(int argc, char **argv)
{
	int sfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sfd < 0) {
		printf("ifconfig: socket() failed\n");
		return 1;
	}

	if (argc == 1) {
		/* Show known interfaces */
		display_if(sfd, "eth0");
		display_if(sfd, "lo");
		close(sfd);
		return 0;
	}

	if (argc == 2) {
		display_if(sfd, argv[1]);
		close(sfd);
		return 0;
	}

	/* Configuration mode: ifconfig iface ip [netmask mask] [up|down] */
	const char *ifname = argv[1];
	struct ifreq ifr;
	int i;

	for (i = 2; i < argc; i++) {
		if (strcmp(argv[i], "up") == 0) {
			memset(&ifr, 0, sizeof(ifr));
			strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name) - 1);
			if (ioctl(sfd, SIOCGIFFLAGS, &ifr) == 0) {
				ifr.ifr_flags |= (IFF_UP | IFF_RUNNING);
				ioctl(sfd, SIOCSIFFLAGS, &ifr);
			}
		} else if (strcmp(argv[i], "down") == 0) {
			memset(&ifr, 0, sizeof(ifr));
			strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name) - 1);
			if (ioctl(sfd, SIOCGIFFLAGS, &ifr) == 0) {
				ifr.ifr_flags &= ~(IFF_UP | IFF_RUNNING);
				ioctl(sfd, SIOCSIFFLAGS, &ifr);
			}
		} else if (strcmp(argv[i], "netmask") == 0 && i + 1 < argc) {
			i++;
			memset(&ifr, 0, sizeof(ifr));
			strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name) - 1);
			struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_netmask;
			sin->sin_family = AF_INET;
			sin->sin_addr.s_addr = inet_addr(argv[i]);
			ioctl(sfd, SIOCSIFNETMASK, &ifr);
		} else {
			/* IP address */
			memset(&ifr, 0, sizeof(ifr));
			strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name) - 1);
			struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;
			sin->sin_family = AF_INET;
			sin->sin_addr.s_addr = inet_addr(argv[i]);
			if (ioctl(sfd, SIOCSIFADDR, &ifr) < 0) {
				printf("ifconfig: error setting address '%s'\n", argv[i]);
			}
			/* Auto UP */
			if (ioctl(sfd, SIOCGIFFLAGS, &ifr) == 0) {
				ifr.ifr_flags |= (IFF_UP | IFF_RUNNING);
				ioctl(sfd, SIOCSIFFLAGS, &ifr);
			}
		}
	}

	close(sfd);
	return 0;
}
