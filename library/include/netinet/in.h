#ifndef _NETINET_IN_H
#define _NETINET_IN_H

#include <sys/socket.h>

typedef unsigned short in_port_t;
typedef unsigned long  in_addr_t;

struct in_addr {
    in_addr_t s_addr;
};

struct sockaddr_in {
    sa_family_t     sin_family;
    in_port_t       sin_port;
    struct in_addr  sin_addr;
    unsigned char   sin_zero[8];
};

#define INADDR_ANY              ((in_addr_t) 0x00000000)
#define INADDR_LOOPBACK         ((in_addr_t) 0x7f000001)
#define INADDR_BROADCAST        ((in_addr_t) 0xffffffff)
#define INADDR_NONE             ((in_addr_t) 0xffffffff)

#define IPPROTO_IP              0
#define IPPROTO_ICMP            1
#define IPPROTO_TCP             6
#define IPPROTO_UDP             17
#define IPPROTO_RAW             255

static inline unsigned short htons(unsigned short x)
{
    return (x << 8) | (x >> 8);
}

static inline unsigned short ntohs(unsigned short x)
{
    return (x << 8) | (x >> 8);
}

static inline unsigned long htonl(unsigned long x)
{
    return ((x & 0xff000000) >> 24) |
           ((x & 0x00ff0000) >> 8) |
           ((x & 0x0000ff00) << 8) |
           ((x & 0x000000ff) << 24);
}

static inline unsigned long ntohl(unsigned long x)
{
    return htonl(x);
}

#endif /* _NETINET_IN_H */
