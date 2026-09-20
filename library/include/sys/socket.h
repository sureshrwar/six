#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H

#include <linux/types.h>

#define AF_UNSPEC       0
#define AF_UNIX         1
#define AF_INET         2
#define AF_AX25         3
#define AF_IPX          4
#define AF_APPLETALK    5

#define PF_UNSPEC       AF_UNSPEC
#define PF_UNIX         AF_UNIX
#define PF_INET         AF_INET

#define SOCK_STREAM     1
#define SOCK_DGRAM      2
#define SOCK_RAW        3
#define SOCK_RDM        4
#define SOCK_SEQPACKET  5

#define SOL_SOCKET      1
#define SO_DEBUG        1
#define SO_REUSEADDR    2
#define SO_TYPE         3
#define SO_ERROR        4
#define SO_DONTROUTE    5
#define SO_BROADCAST    6
#define SO_SNDBUF       7
#define SO_RCVBUF       8
#define SO_KEEPALIVE    9
#define SO_OOBINLINE    10
#define SO_NO_CHECK     11
#define SO_PRIORITY     12
#define SO_LINGER       13
#define SO_BSDCOMPAT    14

typedef unsigned short sa_family_t;

struct sockaddr {
    sa_family_t sa_family;
    char        sa_data[14];
};

struct linger {
    int l_onoff;
    int l_linger;
};

#define SYS_SOCKET      1
#define SYS_BIND        2
#define SYS_CONNECT     3
#define SYS_LISTEN      4
#define SYS_ACCEPT      5
#define SYS_GETSOCKNAME 6
#define SYS_GETPEERNAME 7
#define SYS_SOCKETPAIR  8
#define SYS_SEND        9
#define SYS_RECV        10
#define SYS_SENDTO      11
#define SYS_RECVFROM    12
#define SYS_SHUTDOWN    13
#define SYS_SETSOCKOPT  14
#define SYS_GETSOCKOPT  15
#define SYS_SENDMSG     16
#define SYS_RECVMSG     17

#define SHUT_RD         0
#define SHUT_WR         1
#define SHUT_RDWR       2

int socket(int domain, int type, int protocol);
int bind(int sockfd, const struct sockaddr *addr, int addrlen);
int connect(int sockfd, const struct sockaddr *addr, int addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr *addr, int *addrlen);
int getsockname(int sockfd, struct sockaddr *addr, int *addrlen);
int getpeername(int sockfd, struct sockaddr *addr, int *addrlen);
int socketpair(int domain, int type, int protocol, int sv[2]);
int send(int sockfd, const void *buf, int len, int flags);
int recv(int sockfd, void *buf, int len, int flags);
int sendto(int sockfd, const void *buf, int len, int flags, const struct sockaddr *dest_addr, int addrlen);
int recvfrom(int sockfd, void *buf, int len, int flags, struct sockaddr *src_addr, int *addrlen);
int shutdown(int sockfd, int how);
int setsockopt(int sockfd, int level, int optname, const void *optval, int optlen);
int getsockopt(int sockfd, int level, int optname, void *optval, int *optlen);

#endif /* _SYS_SOCKET_H */
