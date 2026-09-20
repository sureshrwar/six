/*
 * drivers/net/six_net.c
 *
 * Virtual Ethernet driver for SIX (Solaris Interface eXtension).
 * Bridges guest Linux 2.0.11 TCP/IP stack to host unprivileged sockets.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/in.h>

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/io.h>
#include <asm/checksum.h>

#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/route.h>

#include "../../arch/six/kernel/host.h"

#define MAX_CONNS 32

enum six_tcp_state {
	CONN_FREE = 0,
	CONN_CONNECTING,
	CONN_ESTABLISHED,
	CONN_CLOSING
};

struct six_tcp_conn {
	int state;
	int host_fd;
	__u32 guest_ip;
	__u32 dest_ip;
	__u16 guest_port;
	__u16 dest_port;
	__u32 guest_seq;
	__u32 our_seq;
};

static struct six_tcp_conn conns[MAX_CONNS];
static struct device *six_dev = NULL;

static const unsigned char VIRT_ROUTER_MAC[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x02 };

static void inject_tcp(struct device *dev, __u32 saddr, __u32 daddr,
		       __u16 sport, __u16 dport, __u32 seq, __u32 ack_seq,
		       __u8 flags, const void *payload, int payload_len)
{
	int total_len = ETH_HLEN + sizeof(struct iphdr) + sizeof(struct tcphdr) + payload_len;
	struct sk_buff *skb = dev_alloc_skb(total_len + 16);
	if (!skb)
		return;

	skb_reserve(skb, 2);
	unsigned char *data = skb_put(skb, total_len);

	struct ethhdr *eth = (struct ethhdr *)data;
	memcpy(eth->h_dest, dev->dev_addr, ETH_ALEN);
	memcpy(eth->h_source, VIRT_ROUTER_MAC, ETH_ALEN);
	eth->h_proto = htons(ETH_P_IP);

	struct iphdr *iph = (struct iphdr *)(data + ETH_HLEN);
	iph->version = 4;
	iph->ihl = 5;
	iph->tos = 0;
	iph->tot_len = htons(sizeof(struct iphdr) + sizeof(struct tcphdr) + payload_len);
	iph->id = htons(1);
	iph->frag_off = 0;
	iph->ttl = 64;
	iph->protocol = IPPROTO_TCP;
	iph->saddr = saddr;
	iph->daddr = daddr;
	iph->check = 0;
	iph->check = ip_fast_csum((unsigned char *)iph, iph->ihl);

	struct tcphdr *th = (struct tcphdr *)(data + ETH_HLEN + sizeof(struct iphdr));
	memset(th, 0, sizeof(*th));
	th->source = htons(sport);
	th->dest = htons(dport);
	th->seq = htonl(seq);
	th->ack_seq = htonl(ack_seq);
	th->doff = 5;
	if (flags & 0x01) th->fin = 1;
	if (flags & 0x02) th->syn = 1;
	if (flags & 0x04) th->rst = 1;
	if (flags & 0x08) th->psh = 1;
	if (flags & 0x10) th->ack = 1;
	th->window = htons(32768);

	if (payload_len > 0)
		memcpy((unsigned char *)(th + 1), payload, payload_len);

	th->check = 0;
	th->check = csum_tcpudp_magic(saddr, daddr,
				      sizeof(struct tcphdr) + payload_len,
				      IPPROTO_TCP,
				      csum_partial((unsigned char *)th,
						   sizeof(struct tcphdr) + payload_len, 0));

	skb->protocol = eth_type_trans(skb, dev);
	skb->dev = dev;
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	netif_rx(skb);
}

static void inject_arp_reply(struct device *dev, const unsigned char *target_mac,
			     __u32 tip, const unsigned char *sender_mac, __u32 sip)
{
	int total_len = ETH_HLEN + sizeof(struct arphdr) + 20;
	struct sk_buff *skb = dev_alloc_skb(total_len + 16);
	if (!skb)
		return;

	skb_reserve(skb, 2);
	unsigned char *data = skb_put(skb, total_len);

	struct ethhdr *eth = (struct ethhdr *)data;
	memcpy(eth->h_dest, target_mac, ETH_ALEN);
	memcpy(eth->h_source, sender_mac, ETH_ALEN);
	eth->h_proto = htons(ETH_P_ARP);

	struct arphdr *arp = (struct arphdr *)(data + ETH_HLEN);
	arp->ar_hrd = htons(ARPHRD_ETHER);
	arp->ar_pro = htons(ETH_P_IP);
	arp->ar_hln = 6;
	arp->ar_pln = 4;
	arp->ar_op = htons(ARPOP_REPLY);

	unsigned char *arp_ptr = (unsigned char *)(arp + 1);
	memcpy(arp_ptr, sender_mac, 6);
	arp_ptr += 6;
	memcpy(arp_ptr, &sip, 4);
	arp_ptr += 4;
	memcpy(arp_ptr, target_mac, 6);
	arp_ptr += 6;
	memcpy(arp_ptr, &tip, 4);

	skb->protocol = eth_type_trans(skb, dev);
	skb->dev = dev;
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	netif_rx(skb);
}

void six_eth_poll(void)
{
	int i;
	char buf[1400];

	if (!six_dev)
		return;

	for (i = 0; i < MAX_CONNS; i++) {
		struct six_tcp_conn *c = &conns[i];
		if (c->state == CONN_FREE)
			continue;

		if (c->state == CONN_CONNECTING) {
			int st = six_host_net_poll_connected(c->host_fd);
			if (st == 1) {
				c->state = CONN_ESTABLISHED;
				inject_tcp(six_dev, c->dest_ip, c->guest_ip,
					   c->dest_port, c->guest_port,
					   c->our_seq++, c->guest_seq,
					   0x12 /* SYN|ACK */, NULL, 0);
			} else if (st == -1) {
				inject_tcp(six_dev, c->dest_ip, c->guest_ip,
					   c->dest_port, c->guest_port,
					   0, c->guest_seq,
					   0x04 /* RST */, NULL, 0);
				six_host_net_close(c->host_fd);
				c->state = CONN_FREE;
			}
		} else if (c->state == CONN_ESTABLISHED) {
			while (six_host_net_poll_readable(c->host_fd)) {
				int r = six_host_net_recv(c->host_fd, buf, sizeof(buf));
				if (r > 0) {
					inject_tcp(six_dev, c->dest_ip, c->guest_ip,
						   c->dest_port, c->guest_port,
						   c->our_seq, c->guest_seq,
						   0x18 /* ACK|PSH */, buf, r);
					c->our_seq += r;
					if (bh_mask & bh_active)
						do_bottom_half();
				} else if (r == 0) {
					inject_tcp(six_dev, c->dest_ip, c->guest_ip,
						   c->dest_port, c->guest_port,
						   c->our_seq++, c->guest_seq,
						   0x11 /* FIN|ACK */, NULL, 0);
					c->state = CONN_CLOSING;
					six_host_net_close(c->host_fd);
					if (bh_mask & bh_active)
						do_bottom_half();
					break;
				}
			}
		} else if (c->state == CONN_CLOSING) {
			c->state = CONN_FREE;
		}
	}

	if (bh_mask & bh_active)
		do_bottom_half();
}

static int six_eth_xmit(struct sk_buff *skb, struct device *dev)
{
	struct ethhdr *eth = (struct ethhdr *)skb->data;
	unsigned short proto = ntohs(eth->h_proto);

	if (proto == ETH_P_ARP) {
		struct arphdr *arp = (struct arphdr *)(skb->data + ETH_HLEN);
		if (arp->ar_op == htons(ARPOP_REQUEST)) {
			unsigned char *arp_ptr = (unsigned char *)(arp + 1);
			unsigned char *smac = arp_ptr;
			arp_ptr += 6;
			__u32 sip = *(__u32 *)arp_ptr;
			arp_ptr += 4;
			/* skip target mac */
			arp_ptr += 6;
			__u32 tip = *(__u32 *)arp_ptr;

			inject_arp_reply(dev, smac, sip, VIRT_ROUTER_MAC, tip);
		}
		dev_kfree_skb(skb, FREE_WRITE);
		return 0;
	}

	if (proto == ETH_P_IP) {
		struct iphdr *iph = (struct iphdr *)(skb->data + ETH_HLEN);

		if (iph->protocol == IPPROTO_UDP) {
			struct udphdr *uh = (struct udphdr *)((unsigned char *)iph + (iph->ihl << 2));
			if (ntohs(uh->dest) == 53) {
				/* DNS query */
				unsigned char *payload = (unsigned char *)(uh + 1);
				int plen = ntohs(uh->len) - sizeof(struct udphdr);
				char resp[1024];
				int rlen = six_host_net_dns_query(payload, plen, resp, sizeof(resp));
				if (rlen > 0) {
					int tlen = ETH_HLEN + sizeof(struct iphdr) + sizeof(struct udphdr) + rlen;
					struct sk_buff *rskb = dev_alloc_skb(tlen + 16);
					if (rskb) {
						skb_reserve(rskb, 2);
						unsigned char *d = skb_put(rskb, tlen);
						struct ethhdr *reth = (struct ethhdr *)d;
						memcpy(reth->h_dest, dev->dev_addr, ETH_ALEN);
						memcpy(reth->h_source, VIRT_ROUTER_MAC, ETH_ALEN);
						reth->h_proto = htons(ETH_P_IP);

						struct iphdr *riph = (struct iphdr *)(d + ETH_HLEN);
						riph->version = 4; riph->ihl = 5; riph->tos = 0;
						riph->tot_len = htons(sizeof(struct iphdr) + sizeof(struct udphdr) + rlen);
						riph->id = htons(2); riph->frag_off = 0; riph->ttl = 64;
						riph->protocol = IPPROTO_UDP;
						riph->saddr = iph->daddr;
						riph->daddr = iph->saddr;
						riph->check = 0;
						riph->check = ip_fast_csum((unsigned char *)riph, riph->ihl);

						struct udphdr *ruh = (struct udphdr *)(d + ETH_HLEN + sizeof(struct iphdr));
						ruh->source = uh->dest;
						ruh->dest = uh->source;
						ruh->len = htons(sizeof(struct udphdr) + rlen);
						ruh->check = 0;
						memcpy((unsigned char *)(ruh + 1), resp, rlen);

						rskb->protocol = eth_type_trans(rskb, dev);
						rskb->dev = dev;
						rskb->ip_summed = CHECKSUM_UNNECESSARY;
						netif_rx(rskb);
						if (bh_mask & bh_active)
							do_bottom_half();
					}
				}
				dev_kfree_skb(skb, FREE_WRITE);
				return 0;
			}
		}

		if (iph->protocol == IPPROTO_TCP) {
			struct tcphdr *th = (struct tcphdr *)((unsigned char *)iph + (iph->ihl << 2));
			__u16 sport = ntohs(th->source);
			__u16 dport = ntohs(th->dest);
			__u32 saddr = iph->saddr;
			__u32 daddr = iph->daddr;

			struct six_tcp_conn *conn = NULL;
			int i;
			for (i = 0; i < MAX_CONNS; i++) {
				if (conns[i].state != CONN_FREE &&
				    conns[i].guest_port == sport &&
				    conns[i].dest_port == dport &&
				    conns[i].dest_ip == daddr) {
					conn = &conns[i];
					break;
				}
			}

			if (th->syn && !th->ack) {
				/* SYN: allocate new connection */
				for (i = 0; i < MAX_CONNS; i++) {
					if (conns[i].state == CONN_FREE) {
						conn = &conns[i];
						break;
					}
				}
				if (conn) {
					__u32 connect_ip = daddr;
					/* If guest targets gateway 10.0.2.2, forward to host loopback 127.0.0.1 */
					if (daddr == in_aton("10.0.2.2"))
						connect_ip = in_aton("127.0.0.1");

					conn->host_fd = six_host_net_socket(SOCK_STREAM);
					conn->guest_ip = saddr;
					conn->dest_ip = daddr;
					conn->guest_port = sport;
					conn->dest_port = dport;
					conn->guest_seq = ntohl(th->seq) + 1;
					conn->our_seq = 100000;
					conn->state = CONN_CONNECTING;

					six_host_net_connect(conn->host_fd, connect_ip, dport);

					/* Fast-path synchronous connect check */
					int st = 0, tries = 0;
					while (tries < 150) {
						st = six_host_net_poll_connected(conn->host_fd);
						if (st != 0) break;
						six_host_idle_sleep();
						tries++;
					}
					if (st == 1) {
						conn->state = CONN_ESTABLISHED;
						inject_tcp(dev, conn->dest_ip, conn->guest_ip,
							   conn->dest_port, conn->guest_port,
							   conn->our_seq++, conn->guest_seq,
							   0x12 /* SYN|ACK */, NULL, 0);
						if (bh_mask & bh_active)
							do_bottom_half();
					}
				}
			} else if (conn && conn->state == CONN_ESTABLISHED) {
				int ip_hlen = iph->ihl << 2;
				int tcp_hlen = th->doff << 2;
				int plen = ntohs(iph->tot_len) - ip_hlen - tcp_hlen;
				unsigned char *payload = (unsigned char *)th + tcp_hlen;

				if (plen > 0) {
					six_host_net_send(conn->host_fd, payload, plen);
					conn->guest_seq = ntohl(th->seq) + plen;

					/* Poll for immediate response */
					char rbuf[1400];
					int tries = 0;
					int got_data = 0;
					while (tries < 150) {
						while (six_host_net_poll_readable(conn->host_fd)) {
							int r = six_host_net_recv(conn->host_fd, rbuf, sizeof(rbuf));
							if (r > 0) {
								inject_tcp(dev, conn->dest_ip, conn->guest_ip,
									   conn->dest_port, conn->guest_port,
									   conn->our_seq, conn->guest_seq,
									   0x18 /* ACK|PSH */, rbuf, r);
								conn->our_seq += r;
								got_data = 1;
								if (bh_mask & bh_active)
									do_bottom_half();
							} else if (r == 0) {
								inject_tcp(dev, conn->dest_ip, conn->guest_ip,
									   conn->dest_port, conn->guest_port,
									   conn->our_seq++, conn->guest_seq,
									   0x11 /* FIN|ACK */, NULL, 0);
								conn->state = CONN_CLOSING;
								six_host_net_close(conn->host_fd);
								if (bh_mask & bh_active)
									do_bottom_half();
								break;
							}
						}
						if (got_data || conn->state == CONN_CLOSING)
							break;
						six_host_idle_sleep();
						tries++;
					}

					if (!got_data) {
						/* Send plain ACK if no data arrived yet */
						inject_tcp(dev, conn->dest_ip, conn->guest_ip,
							   conn->dest_port, conn->guest_port,
							   conn->our_seq, conn->guest_seq,
							   0x10 /* ACK */, NULL, 0);
						if (bh_mask & bh_active)
							do_bottom_half();
					}
				}
				if (th->fin) {
					conn->guest_seq = ntohl(th->seq) + 1;
					inject_tcp(dev, conn->dest_ip, conn->guest_ip,
						   conn->dest_port, conn->guest_port,
						   conn->our_seq, conn->guest_seq,
						   0x11 /* FIN|ACK */, NULL, 0);
					six_host_net_close(conn->host_fd);
					conn->state = CONN_CLOSING;
				}
			} else if (conn && th->rst) {
				six_host_net_close(conn->host_fd);
				conn->state = CONN_FREE;
			}
		}
	}

	dev_kfree_skb(skb, FREE_WRITE);
	return 0;
}

static int six_eth_open(struct device *dev)
{
	dev->flags |= IFF_UP | IFF_RUNNING;
	return 0;
}

static int six_eth_close(struct device *dev)
{
	dev->flags &= ~(IFF_UP | IFF_RUNNING);
	return 0;
}

int six_eth_init(struct device *dev)
{
	six_dev = dev;

	dev->mtu		= 1500;
	dev->hard_start_xmit	= six_eth_xmit;
	dev->hard_header	= eth_header;
	dev->hard_header_len	= ETH_HLEN;
	dev->addr_len		= ETH_ALEN;
	dev->type		= ARPHRD_ETHER;
	dev->rebuild_header	= eth_rebuild_header;
	dev->open		= six_eth_open;
	dev->stop		= six_eth_close;
	dev->flags		= IFF_BROADCAST | IFF_UP | IFF_RUNNING;
	dev->family		= AF_INET;

	memcpy(dev->dev_addr, "\x52\x54\x00\x12\x34\x56", ETH_ALEN);

	dev->pa_addr		= in_aton("10.0.2.15");
	dev->pa_brdaddr		= in_aton("10.0.2.255");
	dev->pa_mask		= in_aton("255.255.255.0");
	dev->pa_alen		= 4;

	/* Add network route for 10.0.2.0/24 */
	{
		extern int ip_rt_new(struct rtentry *r);
		struct rtentry rt;
		memset(&rt, 0, sizeof(rt));
		((struct sockaddr_in *)&rt.rt_dst)->sin_family = AF_INET;
		((struct sockaddr_in *)&rt.rt_dst)->sin_addr.s_addr = in_aton("10.0.2.0");
		((struct sockaddr_in *)&rt.rt_genmask)->sin_family = AF_INET;
		((struct sockaddr_in *)&rt.rt_genmask)->sin_addr.s_addr = in_aton("255.255.255.0");
		rt.rt_flags = RTF_UP;
		rt.rt_dev = NULL;
		int ret1 = ip_rt_new(&rt);
		printk("six_net: route 10.0.2.0 ret=%d\n", ret1);

		/* Add default gateway route 0.0.0.0 -> 10.0.2.2 */
		memset(&rt, 0, sizeof(rt));
		((struct sockaddr_in *)&rt.rt_dst)->sin_family = AF_INET;
		((struct sockaddr_in *)&rt.rt_dst)->sin_addr.s_addr = 0;
		((struct sockaddr_in *)&rt.rt_gateway)->sin_family = AF_INET;
		((struct sockaddr_in *)&rt.rt_gateway)->sin_addr.s_addr = in_aton("10.0.2.2");
		((struct sockaddr_in *)&rt.rt_genmask)->sin_family = AF_INET;
		((struct sockaddr_in *)&rt.rt_genmask)->sin_addr.s_addr = 0;
		rt.rt_flags = RTF_UP | RTF_GATEWAY;
		rt.rt_dev = NULL;
		int ret2 = ip_rt_new(&rt);
		printk("six_net: route default ret=%d\n", ret2);
	}

	return 0;
}
